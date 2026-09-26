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
  packet.wheel = 0;
  return packet;
}

int8_t MotionProcessor::processScroll(float gy, const MPU6050Driver &sensor) {
  float velY = gy - sensor.offsetY;
  if (fabs(velY) < Config::DEADZONE) {
    return 0;
  }

  // คำนวณ delta ตามอัตราการอ่านเซนเซอร์ (dt = 1 / SENSOR_SAMPLE_RATE_HZ)
  constexpr float dt = 1.0f / static_cast<float>(Config::SENSOR_SAMPLE_RATE_HZ);
  float delta = velY * Config::SCROLL_SENSITIVITY * dt;
  if (Config::INVERT_SCROLL) delta = -delta;

  _scrollAccum += delta;

  // เมื่อสะสมการก้ม-เงยครบ 1 notch ให้ส่งเป็น step ของลูกกลิ้ง
  if (fabs(_scrollAccum) >= 1.0f) {
    int8_t steps = static_cast<int8_t>(_scrollAccum);
    _scrollAccum -= static_cast<float>(steps);
    return constrain(steps, -127, 127);
  }
  return 0;
}

void MotionProcessor::resetScroll() {
  _scrollAccum = 0.0f;
}