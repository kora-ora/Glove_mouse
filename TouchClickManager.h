#ifndef TOUCH_CLICK_MANAGER_H
#define TOUCH_CLICK_MANAGER_H

#include <Arduino.h>

// Bitmask ของปุ่มใน HID report (bit 0 = ซ้าย, bit 1 = ขวา) ตรงกับ Report Map ใน HidMouseService
constexpr uint8_t TOUCH_BUTTON_LEFT  = 0b01;
constexpr uint8_t TOUCH_BUTTON_RIGHT = 0b10;

// Backward-compatibility alias
constexpr uint8_t FLEX_BUTTON_LEFT  = TOUCH_BUTTON_LEFT;
constexpr uint8_t FLEX_BUTTON_RIGHT = TOUCH_BUTTON_RIGHT;

// อ่าน capacitive touch pad 2 จุด (นิ้วชี้/นิ้วกลาง) แปลงเป็น click bitmask สำหรับ MousePacket.buttons
// - ใช้หลักการเดียวกับ TouchTapDetector (แตะ = touchRead < TOUCH_CLICK_THRESHOLD)
// - Debounce ด้วยจำนวน sample ติดกันเพื่อกันสัญญาณรบกวน
// - Momentary: แตะค้าง = ปุ่มกดลง, ปล่อย = ปุ่มคลายตัว (รองรับการลาก Drag & Drop)
class TouchClickManager {
public:
  void begin(uint8_t pinIndex, uint8_t pinMiddle);
  uint8_t update();

  uint32_t rawIndex() const;
  uint32_t rawMiddle() const;

  bool isTouchedNow() const { return _indexFinger.pressed || _middleFinger.pressed; }
  bool isLeftPressed() const { return _indexFinger.pressed; }
  bool isRightPressed() const { return _middleFinger.pressed; }
  bool isBothPressed() const { return _indexFinger.pressed && _middleFinger.pressed; }

private:
  struct FingerTouchState {
    bool pressed = false;
    bool candidateState = false;
    uint8_t debounceCount = 0;
  };

  void updateFinger(FingerTouchState &finger, bool rawTouched);

  uint8_t _pinIndex = 0;
  uint8_t _pinMiddle = 0;

  FingerTouchState _indexFinger;
  FingerTouchState _middleFinger;
};

#endif // TOUCH_CLICK_MANAGER_H
