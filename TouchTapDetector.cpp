#include "TouchTapDetector.h"
#include "Config.h"

TouchTapDetector *TouchTapDetector::s_instance = nullptr;

void TouchTapDetector::begin(uint8_t pin) {
  _pin = pin;
  s_instance = this;
  touchAttachInterrupt(_pin, &TouchTapDetector::isrThunk, Config::TOUCH_THRESHOLD);
}

void IRAM_ATTR TouchTapDetector::isrThunk() {
  if (s_instance) s_instance->onIsr();
}

void IRAM_ATTR TouchTapDetector::onIsr() {
  _lastIsrMs = millis();
}

uint32_t TouchTapDetector::rawValue() const {
  return touchRead(_pin);
}

uint8_t TouchTapDetector::update() {
  const uint32_t now = millis();

  if (now - _lastSampleMs >= Config::TOUCH_SAMPLE_MS) {
    _lastSampleMs = now;

    // ใช้ touchRead() ตรง ๆ (poll) เหมือนเดิม: บาง core version (เช่น esp32 core 3.x)
    // ยิง touchAttachInterrupt callback แค่ครั้งเดียวตอนเริ่มแตะ ไม่ได้ยิงรัวตลอดที่แตะค้าง
    // ทำให้ debounce/tap FSM นี้เห็นเป็นแตะ-ปล่อยหลายรอบถ้าใช้ ISR เป็นแหล่งข้อมูลหลัก
    const bool raw = rawValue() < Config::TOUCH_THRESHOLD;
    if (raw == _candidate) {
      if (_debounce < 255) _debounce++;
    } else {
      _candidate = raw;
      _debounce = 1;
    }

    if (_debounce >= Config::TOUCH_DEBOUNCE_SAMPLES && _candidate != _touched) {
      _touched = _candidate;
      _lastEdgeMs = now;
      if (_touched) _taps++;  // นับตอนเริ่มแตะ
    }
  }

  // ปล่อยนิ้วและเงียบเกินหน้าต่างเวลา -> จบชุดการแตะ
  if (_taps > 0 && !_touched && now - _lastEdgeMs >= Config::TOUCH_TAP_WINDOW_MS) {
    const uint8_t count = _taps >= 2 ? 2 : 1;
    _taps = 0;
    return count;
  }
  return 0;
}
