#ifndef OLED_STATUS_DISPLAY_H
#define OLED_STATUS_DISPLAY_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SH110X.h>
#include "Config.h"
#include "Ds3231Rtc.h"
#include "MouseTypes.h"

// จอ OLED SH1106 แบบ I2C สำหรับแสดงสถานะการใช้งานของ Air Mouse
class OledStatusDisplay {
public:
  bool begin(const Ds3231Rtc *rtc = nullptr);
  void showCalibrating();
  void update(bool connected, int activeSlot, bool paused, const MousePacket &packet);
  void update(bool connected, int activeSlot, const MousePacket &packet) {
    update(connected, activeSlot, false, packet);
  }
  void clear();  // ดับจอ (blank) ตอนเข้า Idle mode

private:
  Adafruit_SH1106G _display{Config::OLED_WIDTH, Config::OLED_HEIGHT, &Wire, -1};
  bool _available = false;
  const Ds3231Rtc *_rtc = nullptr;
  uint8_t _address = Config::OLED_I2C_ADDR;
  uint32_t _lastRefreshAt = 0;
  bool _lastConnected = false;
  int  _lastSlot = -1;
  bool _lastPaused = false;
};

#endif // OLED_STATUS_DISPLAY_H
