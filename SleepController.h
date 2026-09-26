#ifndef SLEEP_CONTROLLER_H
#define SLEEP_CONTROLLER_H

#include <Arduino.h>
#include "OledStatusDisplay.h"

// Software idle mode: turns off OLED display during inactivity to save power
class SleepController {
public:
  void begin(OledStatusDisplay *oled);

  // wakeTrigger = Touch sensors only (can wake from Idle and reset timer)
  // keepAliveTrigger = Mouse motion/click (only resets timer while awake)
  void tick(bool wakeTrigger, bool keepAliveTrigger);

  bool isIdle() const { return _idle; }

private:
  void setIdle(bool enable);

  OledStatusDisplay *_oled = nullptr;
  uint32_t           _lastActivityMs = 0;
  bool               _idle           = false;
};

#endif // SLEEP_CONTROLLER_H
