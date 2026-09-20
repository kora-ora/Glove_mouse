#ifndef MPU6050_DRIVER_H
#define MPU6050_DRIVER_H

#include <Arduino.h>
#include <Wire.h>
#include "Config.h"

class MPU6050Driver {
public:
  MPU6050Driver();

  // เริ่มต้นเซนเซอร์
  bool begin(uint8_t preferredAddress = Config::MPU_DEFAULT_ADDR);

  // ทำการ Calibrate ค่า Offset ขณะมือนิ่ง
  void calibrate(uint16_t samples = Config::CALIB_SAMPLES);

  // อ่านความเร็วเชิงมุม (rad/s)
  bool readGyro(float &gx, float &gy, float &gz) const;

  // เปิดสัญญาณ Interrupt บนชิป MPU6050 (ส่งสัญญาณที่ขา INT ทุกครั้งที่มีข้อมูลใหม่)
  void enableInterrupt();

  // ค่า Offset ของ Gyroscope
  float offsetX;
  float offsetY;
  float offsetZ;
  uint8_t activeAddress;
  uint8_t chipId;

private:
  static constexpr uint8_t REG_SMPLRT_DIV  = 0x19;
  static constexpr uint8_t REG_CONFIG      = 0x1A;
  static constexpr uint8_t REG_GYRO_CONFIG = 0x1B;
  static constexpr uint8_t REG_GYRO_XOUT_H = 0x43;
  static constexpr uint8_t REG_PWR_MGMT_1  = 0x6B;
  static constexpr uint8_t REG_PWR_MGMT_2  = 0x6C;
  static constexpr uint8_t REG_WHO_AM_I    = 0x75;

  bool checkConnection(uint8_t addr) const;
  bool writeRegister(uint8_t reg, uint8_t data) const;
  uint8_t readRegister(uint8_t reg) const;
};

#endif // MPU6050_DRIVER_H
