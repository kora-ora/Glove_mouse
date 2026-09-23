#ifndef FLEX_CLICK_MANAGER_H
#define FLEX_CLICK_MANAGER_H

#include <Arduino.h>

// Bitmask ของปุ่มใน HID report (bit 0 = ซ้าย, bit 1 = ขวา) ตรงกับ Report Map ใน HidMouseService
constexpr uint8_t FLEX_BUTTON_LEFT  = 0b01;
constexpr uint8_t FLEX_BUTTON_RIGHT = 0b10;

// อ่าน flex sensor 2 ตัว (นิ้วชี้/นิ้วกลาง) แปลงเป็น click bitmask สำหรับ MousePacket.buttons
// - Calibrate baseline ครั้งเดียวตอน boot (ไม่ recalibrate ระหว่างใช้งาน กัน drift)
// - Hysteresis + debounce กันสัญญาณกระตุกตรงขอบ threshold
// - Momentary: กด = ปุ่มลง, คลาย = ปุ่มขึ้น ตามสถานะจริงทุกครั้งที่ update()
class FlexClickManager {
public:
  void begin(uint8_t pinIndex, uint8_t pinMiddle);
  void calibrate();
  uint8_t update();

private:
  struct FingerState {
    bool pressed = false;
    bool candidateState = false;
    uint8_t debounceCount = 0;
    float filtered = 0;
    bool connected = true;  // false = ตอน calibrate ค่าแกว่งเกินไป (ไม่ได้ต่อเซนเซอร์/ขาลอย) ปิดการตรวจจับคลิกของนิ้วนี้
  };

  int readFiltered(uint8_t pin, FingerState &finger);
  bool updateFinger(FingerState &finger, int rawValue, int baseline);

  uint8_t _pinIndex = 0;
  uint8_t _pinMiddle = 0;
  int _baselineIndex = 0;
  int _baselineMiddle = 0;

  FingerState _indexFinger;
  FingerState _middleFinger;
};

#endif // FLEX_CLICK_MANAGER_H
