#include "FlexClickManager.h"
#include "Config.h"

void FlexClickManager::begin(uint8_t pinIndex, uint8_t pinMiddle) {
  _pinIndex = pinIndex;
  _pinMiddle = pinMiddle;
  pinMode(_pinIndex, INPUT);
  pinMode(_pinMiddle, INPUT);
}

void FlexClickManager::calibrate() {
  long sumIndex = 0, sumMiddle = 0;
  int loIndex = 4095, hiIndex = 0, loMiddle = 4095, hiMiddle = 0;

  for (int i = 0; i < Config::FLEX_CALIB_SAMPLES; i++) {
    int vIndex = analogRead(_pinIndex);
    int vMiddle = analogRead(_pinMiddle);
    sumIndex += vIndex;
    sumMiddle += vMiddle;
    loIndex = min(loIndex, vIndex);
    hiIndex = max(hiIndex, vIndex);
    loMiddle = min(loMiddle, vMiddle);
    hiMiddle = max(hiMiddle, vMiddle);
    delay(Config::FLEX_CALIB_DELAY_MS);
  }

  _baselineIndex = sumIndex / Config::FLEX_CALIB_SAMPLES;
  _baselineMiddle = sumMiddle / Config::FLEX_CALIB_SAMPLES;

  _indexFinger.filtered = _baselineIndex;
  _middleFinger.filtered = _baselineMiddle;

  // ค่าแกว่งกว้างเกินไประหว่าง calibrate (มือนิ่งอยู่แล้วในขั้นนี้) = ขา ADC ลอย ไม่ได้ต่อเซนเซอร์จริง
  // ปิดการตรวจจับคลิกของนิ้วนั้นไว้ กันคลิกเองจากสัญญาณรบกวน (ต้องรีเซ็ตบอร์ดหลังต่อเซนเซอร์เพื่อ calibrate ใหม่)
  _indexFinger.connected = (hiIndex - loIndex) <= Config::FLEX_CALIB_MAX_RANGE;
  _middleFinger.connected = (hiMiddle - loMiddle) <= Config::FLEX_CALIB_MAX_RANGE;

  Serial.printf("[FLEX] นิ้วชี้  baseline=%d แกว่ง=%d %s\n", _baselineIndex, hiIndex - loIndex,
                _indexFinger.connected ? "" : "-> !! ไม่พบเซนเซอร์ (ขาลอย) ปิดคลิกซ้ายไว้");
  Serial.printf("[FLEX] นิ้วกลาง baseline=%d แกว่ง=%d %s\n", _baselineMiddle, hiMiddle - loMiddle,
                _middleFinger.connected ? "" : "-> !! ไม่พบเซนเซอร์ (ขาลอย) ปิดคลิกขวาไว้");
}

int FlexClickManager::readFiltered(uint8_t pin, FingerState &finger) {
  finger.filtered = Config::FLEX_FILTER_ALPHA * analogRead(pin) +
                     (1 - Config::FLEX_FILTER_ALPHA) * finger.filtered;
  return (int)finger.filtered;
}

bool FlexClickManager::updateFinger(FingerState &finger, int rawValue, int baseline) {
  if (!finger.connected) return false;  // ไม่ได้ต่อเซนเซอร์ (ตรวจตอน calibrate) ไม่ต้องเสี่ยงอ่านสัญญาณรบกวนเป็นคลิก

  // งอนิ้ว -> ค่า ADC ลดลง (ตามวงจร voltage divider ที่ใช้จริง)
  int pressThreshold = baseline - Config::FLEX_PRESS_MARGIN;
  int releaseThreshold = baseline - Config::FLEX_RELEASE_MARGIN;

  bool rawState = finger.pressed;
  if (!finger.pressed && rawValue <= pressThreshold) {
    rawState = true;
  } else if (finger.pressed && rawValue >= releaseThreshold) {
    rawState = false;
  }

  if (rawState == finger.candidateState) {
    finger.debounceCount++;
  } else {
    finger.candidateState = rawState;
    finger.debounceCount = 1;
  }

  if (finger.debounceCount >= Config::FLEX_DEBOUNCE_SAMPLES) {
    finger.pressed = finger.candidateState;
  }

  return finger.pressed;
}

uint8_t FlexClickManager::update() {
  int rawIndex = readFiltered(_pinIndex, _indexFinger);
  int rawMiddle = readFiltered(_pinMiddle, _middleFinger);

  bool leftPressed = updateFinger(_indexFinger, rawIndex, _baselineIndex);
  bool rightPressed = updateFinger(_middleFinger, rawMiddle, _baselineMiddle);

  uint8_t buttons = 0;
  if (leftPressed) buttons |= FLEX_BUTTON_LEFT;
  if (rightPressed) buttons |= FLEX_BUTTON_RIGHT;
  return buttons;
}
