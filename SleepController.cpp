#include "SleepController.h"
#include "Config.h"

void SleepController::begin(OledStatusDisplay *oled) {
  _oled = oled;
  _lastActivityMs = millis();
}

void SleepController::setIdle(bool enable) {
  if (_idle == enable) return;
  _idle = enable;

  if (_idle) {
    if (_oled) _oled->clear();
    Serial.println("💤 [Idle] ไม่มีการใช้งาน เข้าสู่ Idle mode");
  } else {
    Serial.println("🌙 [Idle] ตื่นจาก Idle mode");
  }
}

void SleepController::tick(bool wakeTrigger, bool keepAliveTrigger) {
  const uint32_t now = millis();

  // 1. Wake Trigger (Touch): Can wake from Idle and keep system awake
  if (wakeTrigger) {
    _lastActivityMs = now;
    if (_idle) setIdle(false);
    return;
  }

  // 2. Keep-Alive Trigger (Motion/Click): Only resets timer while already awake
  if (!_idle && keepAliveTrigger) {
    _lastActivityMs = now;
    return;
  }

  // 3. Inactivity Timeout: Enters Idle after timeout
  if (!_idle && (now - _lastActivityMs >= Config::IDLE_TIMEOUT_MS)) {
    setIdle(true);
  }
}
