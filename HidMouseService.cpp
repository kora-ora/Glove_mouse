#include "HidMouseService.h"
#include <NimBLEHIDDevice.h>

// Report Map: เมาส์ 3 ปุ่ม + X/Y แบบ relative 8 บิต (Report ID 1)
// payload 3 ไบต์: [buttons, dx, dy]
static const uint8_t kReportMap[] = {
  0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,  // Usage Page (Generic Desktop), Mouse, Collection (Application)
  0x85, 0x01,                          //   Report ID 1
  0x09, 0x01, 0xA1, 0x00,              //   Pointer, Collection (Physical)
  0x05, 0x09, 0x19, 0x01, 0x29, 0x03,  //     Buttons 1-3
  0x15, 0x00, 0x25, 0x01,              //     0..1
  0x95, 0x03, 0x75, 0x01, 0x81, 0x02,  //     3 bits input
  0x95, 0x01, 0x75, 0x05, 0x81, 0x03,  //     5 bits padding
  0x05, 0x01, 0x09, 0x30, 0x09, 0x31,  //     X, Y
  0x15, 0x81, 0x25, 0x7F,              //     -127..127
  0x75, 0x08, 0x95, 0x02, 0x81, 0x06,  //     2 x 8 bits relative
  0xC0, 0xC0                           //   End Collection x2
};

void HidMouseService::begin(const char *deviceName, void (*beforeStart)(NimBLEServer *)) {
  for (uint8_t i = 0; i < Config::MAX_HOSTS; i++) _handle[i] = NO_CONN;

  NimBLEDevice::init(deviceName);
  // Bonding + Secure Connections แบบ Just Works (เก็บ bond ลง NVS อัตโนมัติ)
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  _server = NimBLEDevice::createServer();
  _server->setCallbacks(this, false);
  _server->advertiseOnDisconnect(false);  // restart เอง เพื่อคุมเงื่อนไข slot ว่าง

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
  return _input->notify(report, sizeof(report), connHandle);
}

bool HidMouseService::send(const MousePacket &packet) {
  // ถ้า host ที่ active หลุด ให้ย้ายไปเครื่องที่ยังต่ออยู่
  int slot = pickConnectedSlot(_active);
  if (slot < 0) return false;
  if (slot != _active) {
    _active = slot;
    Serial.printf("🔁 [BLE] Host หลุด -> Active: %c\n", 'A' + slot);
  }
  return sendReport(_handle[slot], packet.buttons, packet.dx, packet.dy);
}

bool HidMouseService::switchHost() {
  int current = pickConnectedSlot(_active);
  if (current < 0) return false;

  // หา host อื่นที่ต่ออยู่ ถ้าไม่มีก็ไม่ต้องสลับ
  int next = -1;
  for (int i = 1; i < Config::MAX_HOSTS; i++) {
    int candidate = (current + i) % Config::MAX_HOSTS;
    if (_handle[candidate] != NO_CONN) { next = candidate; break; }
  }
  if (next < 0) {
    Serial.println("⚠️ [BLE] ไม่มีเครื่องอื่นให้สลับ");
    return false;
  }

  sendReport(_handle[current], 0, 0, 0);  // ปล่อยปุ่มที่ค้างบนเครื่องเดิมก่อนสลับ
  _active = next;
  Serial.printf("🔀 [BLE] Active: %c\n", 'A' + next);
  return true;
}

void HidMouseService::restartAdvertising() {
  if (_server->getConnectedCount() < Config::MAX_HOSTS) {
    NimBLEDevice::getAdvertising()->start();
  }
}

void HidMouseService::onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
  NimBLEAddress addr = connInfo.getIdAddress();

  // เครื่องเดิมได้ slot เดิม, ไม่งั้นใช้ slot ว่างแรก
  int slot = -1;
  for (int i = 0; i < Config::MAX_HOSTS; i++) {
    if (_hasAddr[i] && _addr[i] == addr && _handle[i] == NO_CONN) { slot = i; break; }
  }
  for (int i = 0; slot < 0 && i < Config::MAX_HOSTS; i++) {
    if (_handle[i] == NO_CONN) slot = i;
  }

  if (slot >= 0) {
    _addr[slot] = addr;
    _hasAddr[slot] = true;
    // เครื่องแรกที่ต่อเข้ามาเป็น active ทันที (ไม่พึ่ง getConnectedCount ที่อาจรวมหรือไม่รวมเครื่องนี้)
    bool otherConnected = pickConnectedSlot(-1) >= 0;
    _handle[slot] = connInfo.getConnHandle();
    if (!otherConnected) _active = slot;
    Serial.printf("✅ [BLE] Host %c ต่อแล้ว (%s)\n", 'A' + slot, addr.toString().c_str());
  }
  server->updateConnParams(connInfo.getConnHandle(), Config::BLE_CONN_INTERVAL_MIN,
                           Config::BLE_CONN_INTERVAL_MAX, 0, Config::BLE_CONN_TIMEOUT);
  restartAdvertising();
}

void HidMouseService::onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) {
  for (int i = 0; i < Config::MAX_HOSTS; i++) {
    if (_handle[i] == connInfo.getConnHandle()) {
      _handle[i] = NO_CONN;
      Serial.printf("❌ [BLE] Host %c หลุด (reason %d)\n", 'A' + i, reason);
    }
  }
  restartAdvertising();
}
