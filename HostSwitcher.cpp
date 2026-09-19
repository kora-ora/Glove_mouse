#include "HostSwitcher.h"
#include "Config.h"

void HostSwitcher::begin(uint8_t pin) {
  _pin = pin;
  pinMode(_pin, INPUT_PULLUP);
}

bool HostSwitcher::update() {
  if (digitalRead(_pin) != LOW) {  // ปล่อยปุ่ม
    _pressedSince = 0;
    _fired = false;
    return false;
  }

  uint32_t now = millis();
  if (_pressedSince == 0) _pressedSince = now;

  if (!_fired && now - _pressedSince >= Config::SWITCH_HOLD_MS) {
    _fired = true;
    return true;
  }
  return false;
}
