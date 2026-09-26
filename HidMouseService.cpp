#include "HidMouseService.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// Report Map: เมาส์ 3 ปุ่ม + X/Y relative 8 บิต
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

static const char *describeReason(int reason) {
  switch (reason) {
    case 520: return "supervision timeout";
    case 531: return "host disconnected";
    case 533: return "host resource exhausted";
    case 534: return "local host terminated";
    case 573: return "MIC failure (re-pair required)";
    case 574: return "connection failed to establish";
    default:  return "unknown error";
  }
}

// ==============================================================================
// 1. Initialization
// ==============================================================================
void HidMouseService::begin(const char *deviceName, void (*beforeStart)(NimBLEServer *)) {
  for (auto &slot : _slots) slot.reset();

  NimBLEDevice::init(deviceName);
  NimBLEDevice::setPower(Config::BLE_TX_POWER_DBM);
  Serial.printf("📡 [BLE] Address: %s\n", NimBLEDevice::getAddress().toString().c_str());

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

// ==============================================================================
// 2. Slot Management Helpers
// ==============================================================================
int HidMouseService::findFreeSlot() const {
  for (int i = 0; i < Config::MAX_HOSTS; ++i) {
    if (!_slots[i].isConnected()) return i;
  }
  return -1;
}

int HidMouseService::slotOfHandle(uint16_t connHandle) const {
  for (int i = 0; i < Config::MAX_HOSTS; ++i) {
    if (_slots[i].handle == connHandle) return i;
  }
  return -1;
}

int HidMouseService::pickConnectedSlot(int preferred) const {
  if (preferred >= 0 && _slots[preferred].isConnected()) return preferred;
  for (int i = 0; i < Config::MAX_HOSTS; ++i) {
    if (_slots[i].isConnected()) return i;
  }
  return -1;
}

bool HidMouseService::isConnected() const {
  return pickConnectedSlot() >= 0;
}

void HidMouseService::restartAdvertising() {
  if (_server->getConnectedCount() < Config::MAX_HOSTS) {
    NimBLEDevice::getAdvertising()->start();
  }
}

// ==============================================================================
// 3. Mouse Input & Host Switching
// ==============================================================================
bool HidMouseService::sendReport(uint16_t connHandle, uint8_t buttons, int8_t dx, int8_t dy) {
  const uint8_t report[3] = {buttons, static_cast<uint8_t>(dx), static_cast<uint8_t>(dy)};
  const int slot = slotOfHandle(connHandle);
  if (slot >= 0) _slots[slot].lastTxMs = millis();
  return _input->notify(report, sizeof(report), connHandle);
}

void HidMouseService::pause() {
  if (_paused) return;
  int slot = pickConnectedSlot(_active);
  if (slot >= 0) sendReport(_slots[slot].handle, 0, 0, 0);
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

  const bool ok = sendReport(_slots[slot].handle, packet.buttons, packet.dx, packet.dy);
  if (ok) _lastButtons = packet.buttons;
  return ok;
}

bool HidMouseService::switchHost() {
  int current = pickConnectedSlot(_active);
  if (current < 0) return false;

  int next = -1;
  for (int i = 1; i < Config::MAX_HOSTS; ++i) {
    int candidate = (current + i) % Config::MAX_HOSTS;
    if (_slots[candidate].isConnected()) {
      next = candidate;
      break;
    }
  }

  if (next < 0) {
    Serial.println("⚠️ [BLE] ไม่มีเครื่องอื่นให้สลับ");
    return false;
  }

  sendReport(_slots[current].handle, 0, 0, 0);
  _lastButtons = 0;
  _active = next;
  Serial.printf("🔀 [BLE] Active: %c\n", 'A' + next);
  return true;
}

// ==============================================================================
// 4. Background Services (Keep-alive, Adv, Parameters)
// ==============================================================================
void HidMouseService::serviceKeepAlive(uint32_t now) {
  if (Config::HID_KEEPALIVE_MS == 0) return;

  for (int i = 0; i < Config::MAX_HOSTS; ++i) {
    if (!_slots[i].isConnected()) continue;
    if (now - _slots[i].lastTxMs < Config::HID_KEEPALIVE_MS) continue;

    uint8_t buttons = (i == _active && !_paused) ? _lastButtons : 0;
    sendReport(_slots[i].handle, buttons, 0, 0);
  }
}

void HidMouseService::serviceAdvertisingCheck(uint32_t now) {
  if (now - _lastAdvCheckMs < Config::BLE_ADV_CHECK_MS) return;
  _lastAdvCheckMs = now;

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  if (_server->getConnectedCount() < Config::MAX_HOSTS && !adv->isAdvertising()) {
    adv->start();
  }
}

void HidMouseService::serviceConnectionParams(uint32_t now) {
  for (int i = 0; i < Config::MAX_HOSTS; ++i) {
    if (!_slots[i].isConnected() || _slots[i].paramsChecked) continue;
    if (now - _slots[i].connectedAtMs < Config::BLE_PARAM_CHECK_DELAY_MS) continue;

    _slots[i].paramsChecked = true;
    NimBLEConnInfo info = _server->getPeerInfoByHandle(_slots[i].handle);
    const uint16_t interval = info.getConnInterval();
    if (interval == 0) continue;

    Serial.printf("📶 [BLE] Host %c interval=%.1f ms latency=%u timeout=%u ms\n",
                  'A' + i, interval * 1.25f, info.getConnLatency(), info.getConnTimeout() * 10);

    if (interval < Config::BLE_CONN_INTERVAL_MIN || interval > Config::BLE_CONN_INTERVAL_MAX) {
      _server->updateConnParams(_slots[i].handle,
                                Config::BLE_CONN_INTERVAL_MIN, Config::BLE_CONN_INTERVAL_MAX,
                                Config::BLE_CONN_LATENCY, Config::BLE_CONN_TIMEOUT);
    }
  }
}

void HidMouseService::service() {
  const uint32_t now = millis();
  serviceKeepAlive(now);
  serviceAdvertisingCheck(now);
  serviceConnectionParams(now);
}

// ==============================================================================
// 5. Server Callbacks
// ==============================================================================
void HidMouseService::onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
  int slot = findFreeSlot();
  if (slot < 0) {
    Serial.printf("⚠️ [BLE] ปฏิเสธการเชื่อมต่อ เกินโควตา %u เครื่อง\n", Config::MAX_HOSTS);
    server->disconnect(connInfo);
    return;
  }

  bool isFirstDevice = (pickConnectedSlot() < 0);

  _slots[slot].handle        = connInfo.getConnHandle();
  _slots[slot].connectedAtMs = millis();
  _slots[slot].lastTxMs      = millis();
  _slots[slot].paramsChecked = false;

  if (isFirstDevice) _active = slot;

  Serial.printf("✅ [BLE] Host %c ต่อแล้ว (Handle: %u)\n", 'A' + slot, _slots[slot].handle);
}

void HidMouseService::onAuthenticationComplete(NimBLEConnInfo &connInfo) {
  int slot = slotOfHandle(connInfo.getConnHandle());
  Serial.printf("🔐 [BLE] Host %c เข้ารหัส=%s bonded=%s\n",
                slot >= 0 ? 'A' + slot : '?',
                connInfo.isEncrypted() ? "ใช่" : "ไม่",
                connInfo.isBonded() ? "ใช่" : "ไม่");
}

void HidMouseService::onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) {
  int slot = slotOfHandle(connInfo.getConnHandle());
  if (slot >= 0) {
    _slots[slot].reset();
    Serial.printf("❌ [BLE] Host %c หลุด (reason %d: %s)\n", 'A' + slot, reason, describeReason(reason));

    if (_active == slot) {
      int next = pickConnectedSlot();
      _active = (next >= 0) ? next : 0;
      Serial.printf("🔀 [BLE] Active host หลุด -> ย้ายไป Active: %c\n", 'A' + _active);
    }
  }
  restartAdvertising();
}
