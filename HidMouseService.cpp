#include "HidMouseService.h"
#include <NimBLEDevice.h>
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
  NimBLEDevice::setPower(Config::BLE_TX_POWER_DBM);  // กำลังส่งสูงขึ้น ลดการหลุดจากสัญญาณอ่อน
  Serial.printf("📡 [BLE] Address: %s\n", NimBLEDevice::getAddress().toString().c_str());
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
  // advertise ห่างขึ้น ไม่แย่งเวลาวิทยุกับลิงก์ที่ต่ออยู่ (ยังพอให้ต่อกลับได้ภายในไม่ถึงวินาที)
  adv->setMinInterval(Config::BLE_ADV_INTERVAL_MIN);
  adv->setMaxInterval(Config::BLE_ADV_INTERVAL_MAX);
  adv->setAdvertisingCompleteCallback([](NimBLEAdvertising *) { Serial.println("📡 [BLE] stack แจ้งว่า advertise จบ"); });

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

void HidMouseService::pause() {
  if (_paused) return;
  int slot = pickConnectedSlot(_active);
  if (slot >= 0) sendReport(_handle[slot], 0, 0, 0);  // ปล่อยปุ่มที่ค้างก่อนหยุด
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

int HidMouseService::slotOfHandle(uint16_t connHandle) const {
  for (int i = 0; i < Config::MAX_HOSTS; i++) {
    if (_handle[i] == connHandle) return i;
  }
  return -1;
}

// อธิบายรหัส reason ของ NimBLE (0x200 + รหัส HCI) ให้อ่านง่ายใน Serial Monitor
static const char *describeReason(int reason) {
  switch (reason) {
    case 520: return "สัญญาณหาย/ไม่ตอบ (supervision timeout)";
    case 531: return "เครื่องที่ต่อ (host) เป็นฝ่ายตัด";
    case 534: return "ESP32 เป็นฝ่ายตัด";
    case 573: return "MIC failure: key เข้ารหัสไม่ตรงกัน (ลบอุปกรณ์แล้ว pair ใหม่)";
    case 574: return "สร้างการเชื่อมต่อไม่สำเร็จ";
    default:  return "";
  }
}

void HidMouseService::service() {
  const uint32_t now = millis();

  // ตราบใดที่ยังมีช่องว่าง ต้อง advertise อยู่เสมอ: การเริ่ม advertise ใน callback อาจล้มเหลวเงียบๆ
  // ทำให้บอร์ด "หายไป" จากการค้นหา ตัวนี้ตรวจซ้ำและเริ่มใหม่ให้
  if (now - _lastAdvCheckMs >= Config::BLE_ADV_CHECK_MS) {
    _lastAdvCheckMs = now;
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (_server->getConnectedCount() < Config::MAX_HOSTS && !adv->isAdvertising()) {
      const bool started = adv->start();
      const bool active = adv->isAdvertising();
      // พิมพ์ไม่เกินทุก 10 วินาที กัน Serial ถูกท่วม (start=ใช่ แต่ active=ไม่ = ตัวควบคุมหยุด advertise ทันที)
      if (now - _lastAdvLogMs >= 10000) {
        _lastAdvLogMs = now;
        Serial.printf("📡 [BLE] advertise หยุดอยู่ -> start=%s ตอนนี้ active=%s ต่ออยู่ %u เครื่อง\n",
                      started ? "ใช่" : "ไม่", active ? "ใช่" : "ไม่", _server->getConnectedCount());
      }
    }
  }

  // หลังต่อเสร็จสักพัก ตรวจ connection interval จริงที่เครื่องนั้นเลือก
  // ขอปรับเฉพาะเมื่อช้าเกินไป (การขอทันทีตอนต่อ ชนกับ procedure ของ host ทำให้ลิงก์หลุดได้)
  for (int i = 0; i < Config::MAX_HOSTS; i++) {
    if (_handle[i] == NO_CONN || _paramsChecked[i]) continue;
    if (now - _connectedAtMs[i] < Config::BLE_PARAM_CHECK_DELAY_MS) continue;
    _paramsChecked[i] = true;

    NimBLEConnInfo info = _server->getPeerInfoByHandle(_handle[i]);
    const uint16_t interval = info.getConnInterval();
    if (interval == 0) continue;  // หาไม่เจอ (เพิ่งหลุด)
    Serial.printf("📶 [BLE] Host %c interval=%.1f ms latency=%u timeout=%u ms\n", 'A' + i,
                  interval * 1.25f, info.getConnLatency(), info.getConnTimeout() * 10);
    if (interval > Config::BLE_CONN_INTERVAL_MAX) {
      _server->updateConnParams(_handle[i], Config::BLE_CONN_INTERVAL_MIN, Config::BLE_CONN_INTERVAL_MAX, 0,
                                Config::BLE_CONN_TIMEOUT);
    }
  }
}

void HidMouseService::onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
  NimBLEAddress addr = connInfo.getIdAddress();

  // เครื่องเดิมได้ slot เดิม, ไม่งั้นใช้ slot ว่างแรก
  int slot = -1;
  for (int i = 0; i < Config::MAX_HOSTS; i++) {
    if (_hasAddr[i] && _addr[i] == addr && _handle[i] == NO_CONN) { slot = i; break; }
  }
  // เครื่องเดิมต่อกลับมาทั้งที่ลิงก์เก่ายังค้างอยู่ (ยังไม่ทันหมดเวลา timeout): ตัดลิงก์เก่าแล้วใช้ slot เดิม
  for (int i = 0; slot < 0 && i < Config::MAX_HOSTS; i++) {
    if (_hasAddr[i] && _addr[i] == addr && _handle[i] != NO_CONN) {
      server->disconnect(_handle[i]);
      slot = i;
    }
  }
  for (int i = 0; slot < 0 && i < Config::MAX_HOSTS; i++) {
    if (_handle[i] == NO_CONN) slot = i;
  }

  if (slot < 0) {
    // เกินจำนวนเครื่องที่รองรับ: ตัดทิ้ง ไม่ปล่อยให้กินช่องเงียบๆ
    Serial.printf("⚠️ [BLE] ปฏิเสธการเชื่อมต่อเกิน %u เครื่อง (%s)\n", Config::MAX_HOSTS, addr.toString().c_str());
    server->disconnect(connInfo);
    return;
  }

  _addr[slot] = addr;
  _hasAddr[slot] = true;
  // เครื่องแรกที่ต่อเข้ามาเป็น active ทันที (ไม่พึ่ง getConnectedCount ที่อาจรวมหรือไม่รวมเครื่องนี้)
  bool otherConnected = pickConnectedSlot(-1) >= 0;
  _connectedAtMs[slot] = millis();
  _paramsChecked[slot] = false;
  _handle[slot] = connInfo.getConnHandle();
  if (!otherConnected) _active = slot;
  Serial.printf("✅ [BLE] Host %c ต่อแล้ว (%s)\n", 'A' + slot, addr.toString().c_str());
  restartAdvertising();
}

void HidMouseService::onAuthenticationComplete(NimBLEConnInfo &connInfo) {
  int slot = slotOfHandle(connInfo.getConnHandle());
  Serial.printf("🔐 [BLE] Host %c เข้ารหัส=%s bonded=%s\n", slot >= 0 ? 'A' + slot : '?',
                connInfo.isEncrypted() ? "ใช่" : "ไม่", connInfo.isBonded() ? "ใช่" : "ไม่");
}

void HidMouseService::onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) {
  for (int i = 0; i < Config::MAX_HOSTS; i++) {
    if (_handle[i] == connInfo.getConnHandle()) {
      _handle[i] = NO_CONN;
      Serial.printf("❌ [BLE] Host %c หลุด หลังต่อ %lu ms (reason %d: %s)\n", 'A' + i,
                    (unsigned long)(millis() - _connectedAtMs[i]), reason, describeReason(reason));

      // เครื่องนั้นตัดทันทีหลังต่อ (ไม่ทันเข้ารหัส/ตรวจ params) ซ้ำๆ = เกือบแน่ว่า key หรือแคช GATT ไม่ตรงกับบอร์ด
      static uint32_t lastHintMs = 0;
      const uint32_t now = millis();
      if (reason == 531 && now - _connectedAtMs[i] < 5000 && now - lastHintMs > 10000) {
        lastHintMs = now;
        Serial.println("   ⚠️ ตัดทันทีหลังต่อ: ลบ 'Glove Air Mouse' ในเครื่องนั้นแล้ว pair ใหม่ (พิมพ์ b ใน Serial Monitor เพื่อล้าง bond ในบอร์ดด้วย)");
      }
    }
  }
  restartAdvertising();
}
