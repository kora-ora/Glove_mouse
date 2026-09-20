#ifndef DS3231_RTC_H
#define DS3231_RTC_H

#include <Arduino.h>
#include <Wire.h>

// ไดรเวอร์ DS3231 แบบใช้ Wire โดยตรง: ไม่เพิ่ม dependency ของ Arduino library
class Ds3231Rtc {
public:
  bool begin();
  bool readTime(char *buffer, size_t bufferSize) const;
  bool isAvailable() const { return _available; }

private:
  static uint8_t bcdToDecimal(uint8_t value);

  bool _available = false;
};

#endif // DS3231_RTC_H
