#include "Buzzer.h"

void Buzzer::begin(uint8_t pin, bool activeHigh) {
  _pin = pin;
  _activeHigh = activeHigh;
  pinMode(_pin, OUTPUT);
  write(false);
}

void Buzzer::write(bool enabled) const {
  digitalWrite(_pin, enabled == _activeHigh ? HIGH : LOW);
}

void Buzzer::beep(uint16_t durationMs) {
  write(true);
  _beeping = true;
  _stopAtMs = millis() + durationMs;
}

void Buzzer::service() {
  if (_beeping && static_cast<int32_t>(millis() - _stopAtMs) >= 0) {
    write(false);
    _beeping = false;
  }
}
