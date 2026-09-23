#include "TouchClickManager.h"
#include "Config.h"

void TouchClickManager::begin(uint8_t pinIndex, uint8_t pinMiddle) {
  _pinIndex = pinIndex;
  _pinMiddle = pinMiddle;
}

uint32_t TouchClickManager::rawIndex() const {
  return touchRead(_pinIndex);
}

uint32_t TouchClickManager::rawMiddle() const {
  return touchRead(_pinMiddle);
}

void TouchClickManager::updateFinger(FingerTouchState &finger, bool rawTouched) {
  if (rawTouched == finger.candidateState) {
    if (finger.debounceCount < 255) finger.debounceCount++;
  } else {
    finger.candidateState = rawTouched;
    finger.debounceCount = 1;
  }

  if (finger.debounceCount >= Config::TOUCH_CLICK_DEBOUNCE_SAMPLES) {
    finger.pressed = finger.candidateState;
  }
}

uint8_t TouchClickManager::update() {
  const bool leftTouched = (rawIndex() < Config::TOUCH_CLICK_THRESHOLD);
  const bool rightTouched = (rawMiddle() < Config::TOUCH_CLICK_THRESHOLD);

  updateFinger(_indexFinger, leftTouched);
  updateFinger(_middleFinger, rightTouched);

  uint8_t buttons = 0;
  if (_indexFinger.pressed) buttons |= TOUCH_BUTTON_LEFT;
  if (_middleFinger.pressed) buttons |= TOUCH_BUTTON_RIGHT;
  return buttons;
}
