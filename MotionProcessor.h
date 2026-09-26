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

  // คำนวณค่า scroll wheel จากการก้ม-เงย (gy) เมื่ออยู่ในโหมด 2-finger scroll
  int8_t processScroll(float gy, const MPU6050Driver &sensor);

  // รีเซ็ตตัวสะสมการเลื่อนเมื่อปล่อยนิ้ว
  void resetScroll();

private:
  float _scrollAccum = 0.0f;
};

#endif // MOTION_PROCESSOR_H