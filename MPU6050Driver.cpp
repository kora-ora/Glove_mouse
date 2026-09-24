#include "MPU6050Driver.h"

MPU6050Driver::MPU6050Driver() 
  : offsetX(0.0f), offsetY(0.0f), offsetZ(0.0f), 
    activeAddress(Config::MPU_DEFAULT_ADDR), chipId(0x00) {
}

bool MPU6050Driver::begin(uint8_t preferredAddress) {
  activeAddress = preferredAddress;
  Serial.printf("🔍 [MPU6050] ตรวจสอบ Address 0x%02X...\n", activeAddress);

  if (!checkConnection(activeAddress)) {
    Serial.printf("❌ [MPU6050] ไม่พบที่ 0x%02X\n", activeAddress);
    return false;
  }

  uint8_t id = 0;
  if (!readRegister(REG_WHO_AM_I, id)) {
    Serial.println("❌ [MPU6050] อ่าน WHO_AM_I ไม่สำเร็จ");
    return false;
  }
  chipId = id;
  if (chipId == 0x00 || chipId == 0xFF) {
    Serial.printf("❌ [MPU6050] WHO_AM_I=0x%02X ผิดปกติ\n", chipId);
    return false;
  }

  Serial.printf("✅ [MPU6050] เชื่อมต่อสำเร็จ! Address: 0x%02X | Chip ID: 0x%02X\n", activeAddress, chipId);

  // รีเซ็ตชิป
  if (!writeRegister(REG_PWR_MGMT_1, 0x80)) {
    Serial.println("❌ [MPU6050] สั่งรีเซ็ตไม่สำเร็จ");
    return false;
  }
  delay(100);

  // ตั้งค่า register: หยุดที่ตัวแรกที่เขียนไม่สำเร็จ
  const bool configured =
      writeRegister(REG_PWR_MGMT_1, 0x01) &&                            // Clock source Gyro X
      writeRegister(REG_PWR_MGMT_2, 0x00) &&                            // Enable all sensors
      writeRegister(REG_GYRO_CONFIG, 0x08) &&                           // +-500 deg/s (65.5 LSB/deg/s)
      writeRegister(REG_CONFIG, 0x03) &&                                // DLPF ~42Hz
      writeRegister(REG_SMPLRT_DIV, 1000 / Config::SENSOR_SAMPLE_RATE_HZ - 1);

  if (!configured) {
    Serial.println("❌ [MPU6050] ตั้งค่า register ไม่สำเร็จ (I2C ไม่ตอบรับ)");
    return false;
  }

  delay(50);
  return true;
}

void MPU6050Driver::enableInterrupt() {
  writeRegister(0x38, 0x01); // Data ready interrupt
  writeRegister(0x37, 0x00); // Active high, push-pull
  Serial.println("✅ [MPU6050] Data Ready Interrupt เปิดใช้งานแล้ว");
}

void MPU6050Driver::calibrate(uint16_t samples) {
  Serial.println("\n[CALIBRATION] กรุณาวางถุงมือนิ่งๆ...");
  delay(1000);

  for (uint8_t attempt = 1; attempt <= Config::CALIB_MAX_ATTEMPTS; attempt++) {
    float sum[3] = {0, 0, 0};
    float lo[3] = {1e9f, 1e9f, 1e9f};
    float hi[3] = {-1e9f, -1e9f, -1e9f};
    uint16_t count = 0;

    for (uint16_t i = 0; i < samples; i++) {
      float g[3];
      if (readGyro(g[0], g[1], g[2])) {
        for (uint8_t a = 0; a < 3; a++) {
          sum[a] += g[a];
          lo[a] = min(lo[a], g[a]);
          hi[a] = max(hi[a], g[a]);
        }
        count++;
      }
      delay(Config::CALIB_DELAY_MS);
    }
    if (count == 0) continue;

    offsetX = sum[0] / count;
    offsetY = sum[1] / count;
    offsetZ = sum[2] / count;

    float range = max(hi[0] - lo[0], max(hi[1] - lo[1], hi[2] - lo[2]));
    if (range < Config::CALIB_MAX_RANGE) {
      Serial.printf("[CALIBRATION] สำเร็จ! Offset: X=%.4f, Y=%.4f, Z=%.4f\n", offsetX, offsetY, offsetZ);
      return;
    }
  }
  Serial.printf("[CALIBRATION] ⚠️ ค่า offset ล่าสุด: X=%.4f, Y=%.4f, Z=%.4f\n", offsetX, offsetY, offsetZ);
}

bool MPU6050Driver::readGyro(float &gx, float &gy, float &gz) const {
  //ใช้เทคนิคการอ่านเดียวกันกับ RTC ที่ต้องส่งสัญญาณเขียนตำแหน่งที่ต้องการไปก่อน ค่อยขออ่าน
  Wire.beginTransmission(activeAddress);
  Wire.write(REG_GYRO_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false; 
  if (Wire.requestFrom(activeAddress, (uint8_t)6) != 6) return false;

  uint8_t hiX = Wire.read(); uint8_t loX = Wire.read();
  uint8_t hiY = Wire.read(); uint8_t loY = Wire.read();
  uint8_t hiZ = Wire.read(); uint8_t loZ = Wire.read();

  int16_t rawX = static_cast<int16_t>((hiX << 8) | loX);
  int16_t rawY = static_cast<int16_t>((hiY << 8) | loY);
  int16_t rawZ = static_cast<int16_t>((hiZ << 8) | loZ);

  //แปลงเป็น rad/s
  constexpr float scaleToRad = (1.0f / 65.5f) * DEG_TO_RAD;
  gx = rawX * scaleToRad;
  gy = rawY * scaleToRad;
  gz = rawZ * scaleToRad;
  return true;
}

bool MPU6050Driver::checkConnection(uint8_t addr) const {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

bool MPU6050Driver::writeRegister(uint8_t reg, uint8_t data) const {
  Wire.beginTransmission(activeAddress);
  Wire.write(reg);
  Wire.write(data);
  return (Wire.endTransmission() == 0);
}

bool MPU6050Driver::readRegister(uint8_t reg, uint8_t &data) const {
  Wire.beginTransmission(activeAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false; // สื่อสารล้มเหลว หรือหาอุปกรณ์ไม่พบ
  }
  if (Wire.requestFrom(activeAddress, (uint8_t)1) != 1) {
    return false; // เซนเซอร์ไม่ตอบสนอง
  }
  data = Wire.read();
  return true;
}
