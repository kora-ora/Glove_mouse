#ifndef HOST_SWITCHER_H
#define HOST_SWITCHER_H

#include <Arduino.h>

// ปุ่มสลับเครื่อง: กดค้างครบ Config::SWITCH_HOLD_MS จึงถือว่า 1 ครั้ง
// (การกดค้างเป็น debounce ในตัว, ต้องปล่อยปุ่มก่อนถึงจะสลับครั้งต่อไปได้)
class HostSwitcher {
public:
  void begin(uint8_t pin);
  bool update();  // คืน true ครั้งเดียวตอนกดค้างครบเวลา

private:
  uint8_t  _pin = 0;
  uint32_t _pressedSince = 0;  // 0 = ไม่ได้กด
  bool     _fired = false;
};

#endif // HOST_SWITCHER_H
