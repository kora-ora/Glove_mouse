#include "MotionProcessor.h"
#include "Config.h"
#include "MPU6050Driver.h"  // ต้องใช้ตัวเต็ม เพราะอ่าน sensor.offsetY/Z

namespace {
// แปลงความเร็วเชิงมุม (rad/s) ของ 1 แกนเป็นระยะขยับ HID [-127, 127]
int8_t axisToDelta(float vel, float sensitivity, bool invert) {
  if (fabs(vel) < Config::DEADZONE) vel = 0.0f;    // Deadzone: ตัดสัญญาณมือสั่น
  int move = static_cast<int>(vel * sensitivity);  // Sensitivity Scaling
  if (invert) move = -move;                        // Inversion
  return static_cast<int8_t>(constrain(move, -127, 127));  // Clamping
}
}  // namespace

MotionProcessor::MotionProcessor() {}

MousePacket MotionProcessor::process(float gx, float gy, float gz, const MPU6050Driver &sensor) {
  // การบิดข้อมือ ซ้าย-ขวา = Z -> แกน X บนจอ, การก้ม-เงย = Y -> แกน Y บนจอ (หัก Offset ก่อน)
  MousePacket packet;
  packet.dx = axisToDelta(gz - sensor.offsetZ, Config::SENSITIVITY_X, Config::INVERT_X);
  packet.dy = axisToDelta(gy - sensor.offsetY, Config::SENSITIVITY_Y, Config::INVERT_Y);
  packet.buttons = 0;  // ยังไม่มีการกดปุ่มในเฟสนี้
  return packet;
}