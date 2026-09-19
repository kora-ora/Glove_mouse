#ifndef TOUCH_TAP_DETECTOR_H
#define TOUCH_TAP_DETECTOR_H

#include <Arduino.h>

// นับจำนวนครั้งที่แตะ capacitive touch pad (ESP32 touch sensor)
// - แตะ = ค่า touchRead < Config::TOUCH_THRESHOLD (debounce ด้วยจำนวน sample ติดกัน)
// - รวมการแตะที่เกิดภายใน Config::TOUCH_TAP_WINDOW_MS เป็นชุดเดียว แล้วรายงานหลังหมดหน้าต่างเวลา
class TouchTapDetector {
public:
  void begin(uint8_t pin);

  // คืน 0 = ไม่มีเหตุการณ์, 1 = แตะครั้งเดียว, 2 = แตะสองครั้งขึ้นไป (เรียกซ้ำได้บ่อย ไม่ block)
  uint8_t update();

  uint32_t rawValue() const;  // ค่า touchRead ปัจจุบัน (ใช้ดูค่าตอนปรับ threshold)

private:
  uint8_t  _pin = 0;
  bool     _touched = false;    // สถานะหลัง debounce
  bool     _candidate = false;  // สถานะดิบล่าสุด
  uint8_t  _debounce = 0;
  uint8_t  _taps = 0;
  uint32_t _lastSampleMs = 0;
  uint32_t _lastEdgeMs = 0;     // เวลาที่สถานะแตะ/ปล่อยเปลี่ยนล่าสุด
};

#endif // TOUCH_TAP_DETECTOR_H
