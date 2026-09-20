#include "SleepController.h"
#include "Config.h"

void SleepController::begin(OledStatusDisplay *oled) {
  _oled = oled;
  _lastActivityMs = millis();
}

void SleepController::tick(bool hadActivity, bool touchedNow) {
  const uint32_t now = millis();

  if (hadActivity || touchedNow) {
    _lastActivityMs = now;
    if (_idle) exitIdle();
    return;
  }

  if (!_idle && now - _lastActivityMs >= Config::IDLE_TIMEOUT_MS) {
    enterIdle();
  }
}

void SleepController::enterIdle() {
  _idle = true;
  if (_oled) _oled->clear();
  Serial.println("💤 [Idle] ไม่มีการใช้งาน เข้าสู่ Idle mode");
}

void SleepController::exitIdle() {
  _idle = false;
  Serial.println("🌙 [Idle] ตื่นจาก Idle mode");
}
