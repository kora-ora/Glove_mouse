#include "Ds3231Rtc.h"

#include "Config.h"

bool Ds3231Rtc::begin() {
  Wire.beginTransmission(Config::RTC_DS3231_ADDR);
  _available = Wire.endTransmission() == 0;
  Serial.println(_available ? "✅ [RTC] DS3231 พร้อมใช้งานที่ I2C address 0x68"
                            : "⚠️ [RTC] ไม่พบ DS3231 ที่ I2C address 0x68");
  return _available;
}

uint8_t Ds3231Rtc::bcdToDecimal(uint8_t value) {
  return static_cast<uint8_t>((value >> 4) * 10 + (value & 0x0F));
}

bool Ds3231Rtc::readTime(char *buffer, size_t bufferSize) const {
  if (!_available || buffer == nullptr || bufferSize < 9) return false;

  Wire.beginTransmission(Config::RTC_DS3231_ADDR);
  Wire.write(static_cast<uint8_t>(0x00));
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(Config::RTC_DS3231_ADDR, static_cast<uint8_t>(3)) != 3) {
    return false;
  }

  const uint8_t seconds = bcdToDecimal(Wire.read() & 0x7F);
  const uint8_t rawMinutes = Wire.read();
  const uint8_t rawHours = Wire.read();
  const uint8_t minutes = bcdToDecimal(rawMinutes & 0x7F);
  uint8_t hours = 0;
  if ((rawHours & 0x40) != 0) {  // 12-hour mode
    hours = bcdToDecimal(rawHours & 0x1F);
    if ((rawHours & 0x20) != 0 && hours != 12) hours += 12;
    if ((rawHours & 0x20) == 0 && hours == 12) hours = 0;
  } else {
    hours = bcdToDecimal(rawHours & 0x3F);
  }

  if (seconds > 59 || minutes > 59 || hours > 23) return false;
  snprintf(buffer, bufferSize, "%02u:%02u:%02u", hours, minutes, seconds);
  return true;
}
