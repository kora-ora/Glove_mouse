#include "ClipboardService.h"
#include <algorithm>

static constexpr size_t HEADER_LEN = 4;
static constexpr uint32_t TX_TIMEOUT_MS = 5000;
static constexpr uint8_t MAX_CHUNKS_PER_SERVICE = 4;

// RAII Helper สำหรับจัดการ FreeRTOS Mutex อัตโนมัติ (ไม่ต้องกลัวลืม xSemaphoreGive)
class MutexGuard {
public:
  explicit MutexGuard(SemaphoreHandle_t mutex) : _mutex(mutex) {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
  }
  ~MutexGuard() {
    if (_mutex) xSemaphoreGive(_mutex);
  }
  MutexGuard(const MutexGuard &) = delete;
  MutexGuard &operator=(const MutexGuard &) = delete;

private:
  SemaphoreHandle_t _mutex;
};

// ==============================================================================
// 1. Packet Protocol Helpers
// ==============================================================================
static void putHeader(uint8_t *out, uint8_t type, uint8_t msgId, uint16_t seq) {
  out[0] = type;
  out[1] = msgId;
  out[2] = seq & 0xFF;
  out[3] = seq >> 8;
}

static inline uint16_t readU16LE(const uint8_t *p) {
  return p[0] | (p[1] << 8);
}

static inline uint32_t readU32LE(const uint8_t *p) {
  return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ==============================================================================
// 2. Lifecycle & Status
// ==============================================================================
void ClipboardService::begin(NimBLEServer *server) {
  _server = server;
  _store.begin();
  _txMutex = xSemaphoreCreateMutex();

  NimBLEService *svc = server->createService(Config::CLIP_SERVICE_UUID);

  NimBLECharacteristic *rx = svc->createCharacteristic(
      Config::CLIP_RX_UUID, NIMBLE_PROPERTY::WRITE);
  rx->setCallbacks(this);

  _tx = svc->createCharacteristic(
      Config::CLIP_TX_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);

  _status = svc->createCharacteristic(
      Config::CLIP_STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  updateStatus();
  Serial.println("📋 [Clipboard] service พร้อม");
}

void ClipboardService::updateStatus(uint16_t skipConn) {
  uint16_t len = _store.size();
  const uint8_t val[3] = { _store.state(), (uint8_t)(len & 0xFF), (uint8_t)(len >> 8) };
  _status->setValue(val, sizeof(val));

  if (skipConn == BLE_HS_CONN_HANDLE_NONE) {
    _status->notify();
  } else {
    // แจ้งเตือนเฉพาะเครื่องอื่นที่ไม่ใช่ผู้ส่งข้อความนี้ ป้องกันเครื่องส่งแย่งดึงข้อมูลตัวเองกลับ
    auto peers = _server->getPeerDevices();
    for (uint16_t h : peers) {
      if (h != skipConn) {
        _status->notify(val, sizeof(val), h);
      }
    }
  }
}

void ClipboardService::sendControl(uint16_t connHandle, PacketType type, uint8_t msgId, ClipErr err) {
  uint8_t pkt[HEADER_LEN + 1];
  putHeader(pkt, type, msgId, 0);

  size_t len = HEADER_LEN;
  if (type == PKT_NACK) {
    pkt[len++] = err;
  }
  _tx->notify(pkt, len, connHandle);
}

// ==============================================================================
// 3. Incoming Packet Handlers (RX Handlers)
// ==============================================================================
void ClipboardService::onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &connInfo) {
  const NimBLEAttValue &val = chr->getValue();
  const uint8_t *p = val.data();
  size_t n = val.size();

  if (n < HEADER_LEN) return;

  uint8_t  type  = p[0];
  uint8_t  msgId = p[1];
  uint16_t seq   = readU16LE(p + 2);
  uint16_t conn  = connInfo.getConnHandle();

  switch (type) {
    case PKT_START: handleStart(conn, msgId, p + HEADER_LEN, n - HEADER_LEN); break;
    case PKT_DATA:  handleData(conn, msgId, seq, p + HEADER_LEN, n - HEADER_LEN); break;
    case PKT_END:   handleEnd(conn, msgId); break;
    case PKT_GET:   startGet(conn, connInfo.getMTU(), msgId); break;
    case PKT_CLEAR: handleClear(conn, msgId); break;
    default:
      sendControl(conn, PKT_NACK, msgId, CLIP_BAD_STATE);
      break;
  }
}

void ClipboardService::handleStart(uint16_t conn, uint8_t msgId, const uint8_t *payload, size_t len) {
  if (len < 6) { // ต้องการ 2 ไบต์ (total) + 4 ไบต์ (crc)
    sendControl(conn, PKT_NACK, msgId, CLIP_BAD_STATE);
    return;
  }

  // ป้องกันการแย่งเขียน (Writer Lock): หากมีเครื่องอื่นกำลังอัปโหลดค้างอยู่และยังไม่หมดเวลา
  if (_writerConn != BLE_HS_CONN_HANDLE_NONE && _writerConn != conn) {
    if (millis() - _writerStartMs < Config::CLIP_RX_TIMEOUT_MS) {
      sendControl(conn, PKT_NACK, msgId, CLIP_BAD_STATE);
      return;
    }
  }

  uint16_t total = readU16LE(payload);
  uint32_t crc   = readU32LE(payload + 2);

  ClipErr err = _store.beginWrite(msgId, total, crc);
  if (err != CLIP_OK) {
    _writerConn = BLE_HS_CONN_HANDLE_NONE;
    sendControl(conn, PKT_NACK, msgId, err);
  } else {
    _writerConn = conn;
    _writerStartMs = millis();
  }
  updateStatus();
}

void ClipboardService::handleData(uint16_t conn, uint8_t msgId, uint16_t seq, const uint8_t *data, size_t len) {
  // ตรวจสอบว่าผู้ส่งคือเครื่องที่ถือ Writer Lock หรือไม่
  if (_writerConn != conn) {
    sendControl(conn, PKT_NACK, msgId, CLIP_BAD_STATE);
    return;
  }

  ClipErr err = _store.append(msgId, seq, data, len);
  if (err != CLIP_OK) {
    _writerConn = BLE_HS_CONN_HANDLE_NONE;
    sendControl(conn, PKT_NACK, msgId, err);
    updateStatus();
  }
}

void ClipboardService::handleEnd(uint16_t conn, uint8_t msgId) {
  if (_writerConn != conn) {
    sendControl(conn, PKT_NACK, msgId, CLIP_BAD_STATE);
    return;
  }
  _writerConn = BLE_HS_CONN_HANDLE_NONE; // ปลดล็อกผู้เขียน

  ClipErr err = _store.commit(msgId);
  sendControl(conn, (err == CLIP_OK) ? PKT_ACK : PKT_NACK, msgId, err);

  if (err == CLIP_OK) {
    updateStatus(conn); // แจ้งเตือนเฉพาะ host เครื่องอื่น ไม่ส่งกลับไปหาเครื่องต้นทางที่ส่งข้อความมา
    Serial.printf("📋 [Clipboard] รับข้อความแล้ว %u ไบต์\n", _store.size());
  } else {
    updateStatus();
  }
}

void ClipboardService::handleClear(uint16_t conn, uint8_t msgId) {
  _writerConn = BLE_HS_CONN_HANDLE_NONE;
  _store.clear();
  sendControl(conn, PKT_ACK, msgId);
  updateStatus();
}

// ==============================================================================
// 4. Outgoing Transmission Handlers (TX State Machine)
// ==============================================================================
void ClipboardService::startGet(uint16_t connHandle, uint16_t mtu, uint8_t msgId) {
  uint16_t total = _store.size();
  if (total == 0) {
    sendControl(connHandle, PKT_NACK, msgId, CLIP_EMPTY);
    return;
  }

  MutexGuard lock(_txMutex);
  if (_send.active) {
    sendControl(connHandle, PKT_NACK, msgId, CLIP_BAD_STATE);
    return;
  }

  int payloadCapacity = (int)mtu - 3 - (int)HEADER_LEN;
  _send = Tx();
  _send.active     = true;
  _send.startMs    = millis();
  _send.connHandle = connHandle;
  _send.msgId      = msgId;
  _send.chunk      = constrain(payloadCapacity, (int)Config::CLIP_MIN_CHUNK, (int)Config::CLIP_MAX_CHUNK);
  _send.total      = total;
  _send.crc        = _store.crc();
}

bool ClipboardService::sendNextChunk() {
  uint8_t pkt[HEADER_LEN + Config::CLIP_MAX_CHUNK];
  size_t len = 0;

  switch (_send.stage) {
    case TX_START: {
      putHeader(pkt, PKT_START, _send.msgId, 0);
      pkt[4] = _send.total & 0xFF;
      pkt[5] = _send.total >> 8;
      memcpy(pkt + 6, &_send.crc, 4);
      len = HEADER_LEN + 6;
      break;
    }
    case TX_DATA: {
      size_t readBytes = _store.copyOut(_send.offset, pkt + HEADER_LEN, _send.chunk);
      if (readBytes == 0) {
        sendControl(_send.connHandle, PKT_NACK, _send.msgId, CLIP_EMPTY);
        _send.active = false;
        return false;
      }
      putHeader(pkt, PKT_DATA, _send.msgId, _send.seq);
      len = HEADER_LEN + readBytes;
      break;
    }
    case TX_END:
    default: {
      putHeader(pkt, PKT_END, _send.msgId, _send.seq);
      len = HEADER_LEN;
      break;
    }
  }

  // หาก BLE Buffer เต็ม ยุติการส่งรอบนี้ชั่วคราว
  if (!_tx->notify(pkt, len, _send.connHandle)) {
    return false;
  }

  // เลื่อน State เมื่อส่งผ่านฉลุย
  if (_send.stage == TX_START) {
    _send.stage = TX_DATA;
  } else if (_send.stage == TX_DATA) {
    _send.offset += len - HEADER_LEN;
    _send.seq++;
    if (_send.offset >= _send.total) {
      _send.stage = TX_END;
    }
  } else {
    _send.active = false; // ส่งครบทุกสถานะแล้ว
  }

  return true;
}

void ClipboardService::service() {
  if (_store.expireIfNeeded()) {
    updateStatus();
    Serial.println("📋 [Clipboard] ล้างข้อความ (หมดเวลา)");
  }

  // ปลดล็อกผู้เขียนหากหมดเวลารับส่ง (Config::CLIP_RX_TIMEOUT_MS)
  if (_writerConn != BLE_HS_CONN_HANDLE_NONE) {
    if (millis() - _writerStartMs > Config::CLIP_RX_TIMEOUT_MS) {
      _writerConn = BLE_HS_CONN_HANDLE_NONE;
      _store.clear();
      updateStatus();
    }
  }

  MutexGuard lock(_txMutex);
  if (!_send.active) return;

  // ตรวจจับ Timeout
  if (millis() - _send.startMs > TX_TIMEOUT_MS) {
    _send.active = false;
    return;
  }

  // ตรวจสอบว่า Client ยังเชื่อมต่ออยู่หรือไม่
  auto peers = _server->getPeerDevices();
  if (std::find(peers.begin(), peers.end(), _send.connHandle) == peers.end()) {
    _send.active = false;
    return;
  }

  // ทยอยส่งตามโควตาต่อรอบ
  for (uint8_t i = 0; i < MAX_CHUNKS_PER_SERVICE && _send.active; ++i) {
    if (!sendNextChunk()) break;
  }
}
