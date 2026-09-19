#include "TouchTapDetector.h"
#include "Config.h"

void TouchTapDetector::begin(uint8_t pin) {
  _pin = pin;
}

uint32_t TouchTapDetector::rawValue() const {
  return touchRead(_pin);
}

uint8_t TouchTapDetector::update() {
  const uint32_t now = millis();

  if (now - _lastSampleMs >= Config::TOUCH_SAMPLE_MS) {
    _lastSampleMs = now;

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
