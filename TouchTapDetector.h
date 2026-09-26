#ifndef TOUCH_TAP_DETECTOR_H
#define TOUCH_TAP_DETECTOR_H

#include <Arduino.h>
#include "Config.h"

// นับจำนวนครั้งที่แตะ capacitive touch pad (ESP32 touch sensor)
// - แตะ = ค่า touchRead < Config::TOUCH_THRESHOLD (debounce ด้วยจำนวน sample ติดกัน)
// - รวมการแตะที่เกิดภายใน Config::TOUCH_TAP_WINDOW_MS เป็นชุดเดียว แล้วรายงานหลังหมดหน้าต่างเวลา
class TouchTapDetector {
public:
  void begin(uint8_t pin);

  // คืน 0 = ไม่มีเหตุการณ์, 1..N = จำนวนครั้งที่แตะติดต่อกันใน tap window (เรียกซ้ำได้บ่อย ไม่ block)
  uint8_t update();

  uint32_t rawValue() const;  // ค่า touchRead ปัจจุบัน (ใช้ดูค่าตอนปรับ threshold)

  // สถานะแตะล่าสุด (ให้ SleepController ใช้ปลุกจาก Idle): OR ระหว่างสถานะหลัง debounce (poll-based, แม่นแต่ช้ากว่าเล็กน้อย)
  // กับ ISR hint (ยิงทันทีตอนเริ่มแตะ แต่บาง core ยิงแค่ครั้งเดียวจึงใช้เป็นแค่สัญญาณเสริมไม่ใช่หลัก)
  bool isTouchedNow() const { return _touched || (millis() - _lastIsrMs) < Config::TOUCH_ISR_WAKE_HINT_MS; }

private:
  static void IRAM_ATTR isrThunk();
  void IRAM_ATTR onIsr();

  static TouchTapDetector *s_instance;

  uint8_t  _pin = 0;
  volatile bool _touched = false;    // สถานะหลัง debounce (เข้าถึงข้าม Core 0 และ Core 1)
  bool     _candidate = false;  // สถานะดิบล่าสุด
  uint8_t  _debounce = 0;
  uint8_t  _taps = 0;
  uint32_t _lastSampleMs = 0;
  uint32_t _lastEdgeMs = 0;     // เวลาที่สถานะแตะ/ปล่อยเปลี่ยนล่าสุด
  volatile uint32_t _lastIsrMs = 0;  // เวลาที่ touchAttachInterrupt ยิงล่าสุด (ใช้เป็น wake hint เสริมเท่านั้น ไม่ใช่แหล่งข้อมูลของ FSM)
};

#endif // TOUCH_TAP_DETECTOR_H
