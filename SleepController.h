#ifndef SLEEP_CONTROLLER_H
#define SLEEP_CONTROLLER_H

#include <Arduino.h>
#include "OledStatusDisplay.h"

// Idle mode แบบซอฟต์: ไม่มีการขยับ/ปุ่ม/แตะต่อเนื่องเกิน Config::IDLE_TIMEOUT_MS
// -> ดับจอ OLED และให้ TaskSensor งดงานหนัก (อ่าน gyro/flex/ส่ง BLE queue)
// BLE ยังเชื่อมต่ออยู่ตลอด ไม่ใช่ chip sleep จริง
class SleepController {
public:
  void begin(OledStatusDisplay *oled);

  // เรียกทุกรอบของ TaskSensor
  // hadActivity = packet รอบนี้มีการขยับหรือปุ่มเปลี่ยน, touchedNow = กำลังแตะอยู่
  void tick(bool hadActivity, bool touchedNow);

  bool isIdle() const { return _idle; }

private:
  void enterIdle();
  void exitIdle();

  OledStatusDisplay *_oled = nullptr;
  bool _idle = false;
  uint32_t _lastActivityMs = 0;
};

#endif // SLEEP_CONTROLLER_H
