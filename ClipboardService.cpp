#include "ClipboardService.h"
#include <algorithm>

static constexpr size_t HEADER_LEN = 4;

static void putHeader(uint8_t *out, uint8_t type, uint8_t msgId, uint16_t seq) {
  out[0] = type;
  out[1] = msgId;
  out[2] = seq & 0xFF;
  out[3] = seq >> 8;
}

void ClipboardService::begin(NimBLEServer *server) {
  _server = server;
  _store.begin();
  _txMutex = xSemaphoreCreateMutex();

  NimBLEService *svc = server->createService(Config::CLIP_SERVICE_UUID);

  // เครื่อง -> ESP32
  NimBLECharacteristic *rx = svc->createCharacteristic(
      Config::CLIP_RX_UUID, NIMBLE_PROPERTY::WRITE);
  rx->setCallbacks(this);

  // ESP32 -> เครื่อง: ACK/NACK และข้อความขากลับ
  _tx = svc->createCharacteristic(
      Config::CLIP_TX_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);

  // สถานะ: [state u8][len u16 LE]
  _status = svc->createCharacteristic(
      Config::CLIP_STATUS_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  updateStatus();

  Serial.println("📋 [Clipboard] service พร้อม");
}

void ClipboardService::updateStatus() {
  uint16_t len = _store.size();
  const uint8_t value[3] = {_store.state(), static_cast<uint8_t>(len & 0xFF), static_cast<uint8_t>(len >> 8)};
  _status->setValue(value, sizeof(value));
  _status->notify();
}

void ClipboardService::sendControl(uint16_t connHandle, PacketType type, uint8_t msgId, ClipErr err) {
  uint8_t pkt[HEADER_LEN + 1];
  putHeader(pkt, type, msgId, 0);
  size_t len = HEADER_LEN;
  if (type == PKT_NACK) pkt[len++] = err;
  _tx->notify(pkt, len, connHandle);
}

void ClipboardService::startGet(uint16_t connHandle, uint16_t mtu, uint8_t msgId) {
  uint16_t total = _store.size();
  if (total == 0) {
    sendControl(connHandle, PKT_NACK, msgId, CLIP_EMPTY);
    return;
  }

  xSemaphoreTake(_txMutex, portMAX_DELAY);
  if (_send.active) {
    xSemaphoreGive(_txMutex);
    sendControl(connHandle, PKT_NACK, msgId, CLIP_BAD_STATE);  // กำลังส่งอยู่
    return;
  }
  int chunk = (int)mtu - 3 - (int)HEADER_LEN;
  _send = Tx();
  _send.active = true;
  _send.connHandle = connHandle;
  _send.msgId = msgId;
  _send.chunk = constrain(chunk, (int)Config::CLIP_MIN_CHUNK, (int)Config::CLIP_MAX_CHUNK);
  _send.total = total;
  _send.crc = _store.crc();
  xSemaphoreGive(_txMutex);
}

void ClipboardService::onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &connInfo) {
  const NimBLEAttValue &value = chr->getValue();
  const uint8_t *p = value.data();
  size_t n = value.size();
  if (n < HEADER_LEN) return;

  uint8_t  type  = p[0];
  uint8_t  msgId = p[1];
  uint16_t seq   = p[2] | (p[3] << 8);
  uint16_t conn  = connInfo.getConnHandle();

  switch (type) {
    case PKT_START: {
      if (n < HEADER_LEN + 6) { sendControl(conn, PKT_NACK, msgId, CLIP_BAD_STATE); break; }
      uint16_t total = p[4] | (p[5] << 8);
      uint32_t crc   = p[6] | (p[7] << 8) | ((uint32_t)p[8] << 16) | ((uint32_t)p[9] << 24);
      ClipErr err = _store.beginWrite(msgId, total, crc);
      if (err != CLIP_OK) sendControl(conn, PKT_NACK, msgId, err);
      updateStatus();
      break;
    }
    case PKT_DATA: {
      ClipErr err = _store.append(msgId, seq, p + HEADER_LEN, n - HEADER_LEN);
      if (err != CLIP_OK) { sendControl(conn, PKT_NACK, msgId, err); updateStatus(); }
      break;
    }
    case PKT_END: {
      ClipErr err = _store.commit(msgId);
      sendControl(conn, err == CLIP_OK ? PKT_ACK : PKT_NACK, msgId, err);
      updateStatus();
      if (err == CLIP_OK) Serial.printf("📋 [Clipboard] รับข้อความแล้ว %u ไบต์\n", _store.size());
      break;
    }
    case PKT_GET:
      startGet(conn, connInfo.getMTU(), msgId);
      break;
    case PKT_CLEAR:
      _store.clear();
      sendControl(conn, PKT_ACK, msgId);
      updateStatus();
      break;
    default:
      sendControl(conn, PKT_NACK, msgId, CLIP_BAD_STATE);
      break;
  }
}

void ClipboardService::service() {
  if (_store.expireIfNeeded()) {
    updateStatus();
    Serial.println("📋 [Clipboard] ล้างข้อความ (หมดเวลา)");
  }

  xSemaphoreTake(_txMutex, portMAX_DELAY);
  if (!_send.active) {
    xSemaphoreGive(_txMutex);
    return;
  }

  // ผู้ขอหลุดไปแล้ว: เลิกส่ง
  std::vector<uint16_t> peers = _server->getPeerDevices();
  if (std::find(peers.begin(), peers.end(), _send.connHandle) == peers.end()) {
    _send.active = false;
    xSemaphoreGive(_txMutex);
    return;
  }

  // ส่งหลายชิ้นต่อรอบ (ลูปหลักตื่นทุก ~10 ms) หยุดเมื่อ notify ไม่รับ แล้วลองใหม่รอบหน้า
  for (int i = 0; i < 4 && _send.active; i++) {
    uint8_t pkt[HEADER_LEN + Config::CLIP_MAX_CHUNK];
    size_t len = 0;

    if (_send.stage == TX_START) {
      putHeader(pkt, PKT_START, _send.msgId, 0);
      pkt[4] = _send.total & 0xFF;
      pkt[5] = _send.total >> 8;
      memcpy(pkt + 6, &_send.crc, 4);  // ESP32 เป็น little-endian
      len = HEADER_LEN + 6;
    } else if (_send.stage == TX_DATA) {
      size_t got = _store.copyOut(_send.offset, pkt + HEADER_LEN, _send.chunk);
      if (got == 0) {  // ข้อความถูกล้างระหว่างส่ง
        sendControl(_send.connHandle, PKT_NACK, _send.msgId, CLIP_EMPTY);
        _send.active = false;
        break;
      }
      putHeader(pkt, PKT_DATA, _send.msgId, _send.seq);
      len = HEADER_LEN + got;
    } else {
      putHeader(pkt, PKT_END, _send.msgId, _send.seq);
      len = HEADER_LEN;
    }

    if (!_tx->notify(pkt, len, _send.connHandle)) break;

    if (_send.stage == TX_START) {
      _send.stage = TX_DATA;
    } else if (_send.stage == TX_DATA) {
      _send.offset += len - HEADER_LEN;
      _send.seq++;
      if (_send.offset >= _send.total) _send.stage = TX_END;
    } else {
      _send.active = false;
    }
  }
  xSemaphoreGive(_txMutex);
}
