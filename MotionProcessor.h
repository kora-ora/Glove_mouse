#ifndef MOTION_PROCESSOR_H
#define MOTION_PROCESSOR_H

#include <Arduino.h>
#include "Config.h"
#include "MPU6050Driver.h"
#include "MouseTypes.h"

class MotionProcessor {
public:
  MotionProcessor();

  // แปลงความเร็วเชิงมุมของไจโรเป็นระยะขยับของเคอร์เซอร์เมาส์ (MousePacket)
  MousePacket process(float gx, float gy, float gz, const MPU6050Driver &sensor);
};

#endif // MOTION_PROCESSOR_H
