#include "HidMouseService.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// Report Map: เมาส์ 3 ปุ่ม + X/Y แบบ relative 8 บิต (Report ID 1)
static const uint8_t kReportMap[] = {
  0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
  0x85, 0x01,
  0x09, 0x01, 0xA1, 0x00,
  0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
  0x15, 0x00, 0x25, 0x01,
  0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x05, 0x81, 0x03,
  0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
  0x15, 0x81, 0x25, 0x7F,
  0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
  0xC0, 0xC0
};

void HidMouseService::begin(const char *deviceName, void (*beforeStart)(NimBLEServer *)) {
  for (uint8_t i = 0; i < Config::MAX_HOSTS; i++) _handle[i] = NO_CONN;

  NimBLEDevice::init(deviceName);
  NimBLEDevice::setPower(Config::BLE_TX_POWER_DBM);
  Serial.printf("📡 [BLE] Address: %s\n", NimBLEDevice::getAddress().toString().c_str());

  // Just Works legacy pairing
  NimBLEDevice::setSecurityAuth(true, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  _server = NimBLEDevice::createServer();
  _server->setCallbacks(this, false);
  _server->advertiseOnDisconnect(false);

  NimBLEHIDDevice hid(_server);
  _input = hid.getInputReport(1);
  hid.setManufacturer("ESP32");
  hid.setPnp(0x02, 0xE502, 0xA111, 0x0210);
  hid.setHidInfo(0x00, 0x01);
  hid.setReportMap(const_cast<uint8_t *>(kReportMap), sizeof(kReportMap));
  hid.setBatteryLevel(100);

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(Config::HID_APPEARANCE_MOUSE);
  adv->addServiceUUID(hid.getHidService()->getUUID());
  adv->setName(deviceName);
  adv->setMinInterval(Config::BLE_ADV_INTERVAL_MIN);
  adv->setMaxInterval(Config::BLE_ADV_INTERVAL_MAX);

  if (beforeStart) beforeStart(_server);

  _server->start();
  restartAdvertising();
}

bool HidMouseService::isConnected() const {
  return pickConnectedSlot(-1) >= 0;
}

int HidMouseService::pickConnectedSlot(int preferred) const {
  if (preferred >= 0 && _handle[preferred] != NO_CONN) return preferred;
  for (int i = 0; i < Config::MAX_HOSTS; i++) {
    if (_handle[i] != NO_CONN) return i;
  }
  return -1;
}

bool HidMouseService::sendReport(uint16_t connHandle, uint8_t buttons, int8_t dx, int8_t dy) {
  const uint8_t report[3] = {buttons, static_cast<uint8_t>(dx), static_cast<uint8_t>(dy)};
  const int slot = slotOfHandle(connHandle);
  if (slot >= 0) _lastTxMs[slot] = millis();
  return _input->notify(report, sizeof(report), connHandle);
}

void HidMouseService::pause() {
  if (_paused) return;
  int slot = pickConnectedSlot(_active);
  if (slot >= 0) sendReport(_handle[slot], 0, 0, 0);
  _lastButtons = 0;
  _paused = true;
  Serial.println("⏸️ [STATE] หยุดทำงาน");
}

void HidMouseService::resume() {
  if (!_paused) return;
  _paused = false;
  Serial.printf("▶️ [STATE] ทำงานบนเครื่อง %c\n", 'A' + _active);
}

bool HidMouseService::send(const MousePacket &packet) {
  if (_paused) return false;

  int slot = pickConnectedSlot(_active);
  if (slot < 0) return false;
  if (slot != _active) {
    _active = slot;
    Serial.printf("🔁 [BLE] Host หลุด -> Active: %c\n", 'A' + slot);
  }
  const bool ok = sendReport(_handle[slot], packet.buttons, packet.dx, packet.dy);
  if (ok) _lastButtons = packet.buttons;
  return ok;
}

bool HidMouseService::switchHost() {
  int current = pickConnectedSlot(_active);
  if (current < 0) return false;

  int next = -1;
  for (int i = 1; i < Config::MAX_HOSTS; i++) {
    int candidate = (current + i) % Config::MAX_HOSTS;
    if (_handle[candidate] != NO_CONN) { next = candidate; break; }
  }
  if (next < 0) {
    Serial.println("⚠️ [BLE] ไม่มีเครื่องอื่นให้สลับ");
    return false;
  }

  sendReport(_handle[current], 0, 0, 0);
  _lastButtons = 0;
  _active = next;
  Serial.printf("🔀 [BLE] Active: %c\n", 'A' + next);
  return true;
}

void HidMouseService::restartAdvertising() {
  if (_server->getConnectedCount() < Config::MAX_HOSTS) {
    NimBLEDevice::getAdvertising()->start();
  }
}

int HidMouseService::slotOfHandle(uint16_t connHandle) const {
  for (int i = 0; i < Config::MAX_HOSTS; i++) {
    if (_handle[i] == connHandle) return i;
  }
  return -1;
}

static const char *describeReason(int reason) {
  switch (reason) {
    case 520: return "supervision timeout";
    case 531: return "host disconnected";
    case 533: return "host resource exhausted";
    case 534: return "local host terminated";
    case 573: return "MIC failure (re-pair required)";
    case 574: return "connection failed to establish";
    default:  return "";
  }
}

void HidMouseService::service() {
  const uint32_t now = millis();

  // HID Keep-alive
  if (Config::HID_KEEPALIVE_MS > 0) {
    for (int i = 0; i < Config::MAX_HOSTS; i++) {
      if (_handle[i] == NO_CONN || now - _lastTxMs[i] < Config::HID_KEEPALIVE_MS) continue;
      const uint8_t buttons = (i == _active && !_paused) ? _lastButtons : 0;
      sendReport(_handle[i], buttons, 0, 0);
    }
  }

  // ตรวจสอบและ restart advertise หากยังมีช่องว่าง
  if (now - _lastAdvCheckMs >= Config::BLE_ADV_CHECK_MS) {
    _lastAdvCheckMs = now;
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (_server->getConnectedCount() < Config::MAX_HOSTS && !adv->isAdvertising()) {
      adv->start();
    }
  }

  // ปรับ connection interval ให้อยู่ในช่วงที่เหมาะสม และเปิด Slave Latency
  for (int i = 0; i < Config::MAX_HOSTS; i++) {
    if (_handle[i] == NO_CONN || _paramsChecked[i]) continue;
    if (now - _connectedAtMs[i] < Config::BLE_PARAM_CHECK_DELAY_MS) continue;
    _paramsChecked[i] = true;

    NimBLEConnInfo info = _server->getPeerInfoByHandle(_handle[i]);
    const uint16_t interval = info.getConnInterval();
    if (interval == 0) continue;

    Serial.printf("📶 [BLE] Host %c interval=%.1f ms latency=%u timeout=%u ms\n", 'A' + i,
                  interval * 1.25f, info.getConnLatency(), info.getConnTimeout() * 10);
    if (interval < Config::BLE_CONN_INTERVAL_MIN || interval > Config::BLE_CONN_INTERVAL_MAX) {
      _server->updateConnParams(_handle[i], Config::BLE_CONN_INTERVAL_MIN, Config::BLE_CONN_INTERVAL_MAX,
                                Config::BLE_CONN_LATENCY, Config::BLE_CONN_TIMEOUT);
    }
  }
}

void HidMouseService::onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
  NimBLEAddress addr = connInfo.getIdAddress();
  const bool addrKnown = !addr.isNull();

  int slot = -1;
  if (addrKnown) {
    for (int i = 0; i < Config::MAX_HOSTS; i++) {
      if (_hasAddr[i] && _addr[i] == addr && _handle[i] == NO_CONN) { slot = i; break; }
    }
    for (int i = 0; slot < 0 && i < Config::MAX_HOSTS; i++) {
      if (_hasAddr[i] && _addr[i] == addr && _handle[i] != NO_CONN) {
        server->disconnect(_handle[i]);
        slot = i;
      }
    }
  }
  for (int i = 0; slot < 0 && i < Config::MAX_HOSTS; i++) {
    if (_handle[i] == NO_CONN) slot = i;
  }

  if (slot < 0) {
    Serial.printf("⚠️ [BLE] ปฏิเสธการเชื่อมต่อเกิน %u เครื่อง (%s)\n", Config::MAX_HOSTS, addr.toString().c_str());
    server->disconnect(connInfo);
    return;
  }

  if (addrKnown) {
    _addr[slot] = addr;
    _hasAddr[slot] = true;
  }
  bool otherConnected = pickConnectedSlot(-1) >= 0;
  _connectedAtMs[slot] = millis();
  _lastTxMs[slot] = millis();
  _paramsChecked[slot] = false;
  _handle[slot] = connInfo.getConnHandle();
  if (!otherConnected) _active = slot;
  Serial.printf("✅ [BLE] Host %c ต่อแล้ว (%s)\n", 'A' + slot, addr.toString().c_str());
}

void HidMouseService::onAuthenticationComplete(NimBLEConnInfo &connInfo) {
  int slot = slotOfHandle(connInfo.getConnHandle());
  if (slot >= 0) {
    NimBLEAddress idAddr = connInfo.getIdAddress();
    if (!idAddr.isNull()) {
      _addr[slot] = idAddr;
      _hasAddr[slot] = true;
    }
  }
  Serial.printf("🔐 [BLE] Host %c เข้ารหัส=%s bonded=%s\n", slot >= 0 ? 'A' + slot : '?',
                connInfo.isEncrypted() ? "ใช่" : "ไม่", connInfo.isBonded() ? "ใช่" : "ไม่");
}

void HidMouseService::onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) {
  for (int i = 0; i < Config::MAX_HOSTS; i++) {
    if (_handle[i] == connInfo.getConnHandle()) {
      _handle[i] = NO_CONN;
      Serial.printf("❌ [BLE] Host %c หลุด (reason %d: %s)\n", 'A' + i, reason, describeReason(reason));

      // ถ้าเครื่อง active หลุด ให้สลับไปเครื่องที่ยังต่ออยู่ทันที
      if (_active == i) {
        int nextSlot = pickConnectedSlot(-1);
        _active = (nextSlot >= 0) ? nextSlot : 0;
        Serial.printf("🔀 [BLE] Active host หลุด -> ย้ายไป Active: %c\n", 'A' + _active);
      }
    }
  }
  restartAdvertising();
}
