#ifndef MOTION_PROCESSOR_H
#define MOTION_PROCESSOR_H

#include <Arduino.h>
#include "MouseTypes.h"

class MPU6050Driver;  // forward declaration: ใช้แค่ reference จึงไม่ต้อง include ทั้งไฟล์

class MotionProcessor {
public:
  MotionProcessor();

  // แปลงความเร็วเชิงมุมของไจโรเป็นระยะขยับของเคอร์เซอร์เมาส์ (MousePacket)
  MousePacket process(float gx, float gy, float gz, const MPU6050Driver &sensor);
};

#endif // MOTION_PROCESSOR_H