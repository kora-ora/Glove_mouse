#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>

// Active buzzer แบบ non-blocking เพื่อไม่ให้ BLE HID สะดุดระหว่างส่งเสียง
class Buzzer {
public:
  void begin(uint8_t pin, bool activeHigh);
  void beep(uint16_t durationMs);
  void service();

private:
  void write(bool enabled) const;

  uint8_t _pin = 0;
  bool _activeHigh = true;
  bool _beeping = false;
  uint32_t _stopAtMs = 0;
};

#endif // BUZZER_H
