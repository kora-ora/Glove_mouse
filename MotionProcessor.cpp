#include "MotionProcessor.h"

MotionProcessor::MotionProcessor() {}

MousePacket MotionProcessor::process(float gx, float gy, float gz, const MPU6050Driver &sensor) {
  // 1. หักลบค่า Offset (การบิดข้อมือ ซ้าย-ขวา = Z, การก้ม-เงย = Y)
  float velX = gz - sensor.offsetZ;
  float velY = gy - sensor.offsetY;

  // 2. Deadzone Filter: ตัดสัญญาณมือสั่นเมื่ออยู่นิ่ง
  if (fabs(velX) < Config::DEADZONE) velX = 0.0f;
  if (fabs(velY) < Config::DEADZONE) velY = 0.0f;

  // 3. Sensitivity Scaling & Inversion
  int moveX = static_cast<int>(velX * Config::SENSITIVITY_X);
  int moveY = static_cast<int>(velY * Config::SENSITIVITY_Y);

  if (Config::INVERT_X) moveX = -moveX;
  if (Config::INVERT_Y) moveY = -moveY;

  // 4. Clamping ช่วงค่าสำหรับ HID Mouse [-127, 127]
  moveX = constrain(moveX, -127, 127);
  moveY = constrain(moveY, -127, 127);

  MousePacket packet;
  packet.dx = static_cast<int8_t>(moveX);
  packet.dy = static_cast<int8_t>(moveY);
  packet.buttons = 0; // ยังไม่มีการกดปุ่มในเฟสนี้

  return packet;
}
