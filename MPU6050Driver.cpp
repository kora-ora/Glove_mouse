#include "MPU6050Driver.h"

MPU6050Driver::MPU6050Driver() 
  : offsetX(0.0f), offsetY(0.0f), offsetZ(0.0f), 
    activeAddress(Config::MPU_DEFAULT_ADDR), chipId(0x00) {
}

bool MPU6050Driver::begin(uint8_t preferredAddress) {
  activeAddress = preferredAddress;
  Serial.printf("🔍 [MPU6050] กำลังตรวจสอบที่ Address 0x%02X...\n", activeAddress);

  // ตรวจสอบ I2C ACK หากไม่พบที่ 0x68 จะสลับไป 0x69 อัตโนมัติ
  if (!checkConnection(activeAddress)) {
    if (activeAddress == Config::MPU_DEFAULT_ADDR && checkConnection(Config::MPU_BACKUP_ADDR)) {
      activeAddress = Config::MPU_BACKUP_ADDR;
      Serial.printf("✅ [MPU6050] ตรวจพบที่ Address สำรอง 0x%02X\n", activeAddress);
    } else {
      Serial.println("❌ [MPU6050] ไม่พบอุปกรณ์บน I2C Bus! กรุณาตรวจสอบสายและไฟเลี้ยง");
      return false;
    }
  }

  // อ่านค่า Device ID (WHO_AM_I)
  chipId = readRegister(REG_WHO_AM_I);
  Serial.printf("✅ [MPU6050] เชื่อมต่อสำเร็จ! Address: 0x%02X | Chip ID: 0x%02X\n", activeAddress, chipId);

  // รีเซ็ตชิปก่อนตั้งค่าเสมอ: รีจิสเตอร์ของ MPU6050 ไม่หายเมื่อ ESP32 รีเซ็ต (ชิปยังมีไฟเลี้ยง)
  // ค่าค้างจากรอบก่อน เช่น gyro standby / low-power cycle จะทำให้อ่านค่าได้ 0 ทุกแกน
  writeRegister(REG_PWR_MGMT_1, 0x80);  // DEVICE_RESET
  delay(100);

  // ตั้งค่า Register:
  // 1. ปลุกชิป + เลือก Gyro X เป็น Clock Source เพื่อเสถียรภาพ
  writeRegister(REG_PWR_MGMT_1, 0x01);
  // เปิดทุกแกนของ accel/gyro และปิด low-power wake (ล้าง STBY_* ที่อาจค้างมา)
  writeRegister(REG_PWR_MGMT_2, 0x00);
  // 2. กำหนดย่านวัด Gyroscope +-500 deg/s (65.5 LSB / deg/s)
  writeRegister(REG_GYRO_CONFIG, 0x08);
  // 3. เปิด Low Pass Filter (DLPF) ~42Hz เพื่อกรอง Noise ความถี่สูง
  writeRegister(REG_CONFIG, 0x03);
  // 4. ตั้ง Sample Rate: เมื่อเปิด DLPF ฐานคือ 1 kHz -> Rate = 1000 / (1 + SMPLRT_DIV)
  writeRegister(REG_SMPLRT_DIV, 1000 / Config::SENSOR_SAMPLE_RATE_HZ - 1);

  delay(50);
  return true;
}

void MPU6050Driver::enableInterrupt() {
  // INT_ENABLE (0x38): เปิด Data Ready Interrupt (bit 0 = 1)
  // ทำให้ขา INT ของ MPU6050 ส่งสัญญาณ RISING ทุกครั้งที่ข้อมูลใหม่พร้อมอ่าน
  writeRegister(0x38, 0x01);
  // INT_PIN_CFG (0x37): กำหนดให้ขา INT เป็น Active High, Push-Pull, Auto-clear เมื่อถูกอ่าน
  writeRegister(0x37, 0x00);
  Serial.println("✅ [MPU6050] Data Ready Interrupt เปิดใช้งานแล้ว");
}


void MPU6050Driver::calibrate(uint16_t samples) {
  Serial.println("\n[CALIBRATION] กรุณาวางถุงมือให้นิ่ง 2 วินาที...");
  delay(1000);

  // วัดซ้ำจนกว่าค่าจะนิ่งจริง (ช่วงกว้างของค่าต่อแกน < CALIB_MAX_RANGE)
  // ถ้าขยับระหว่างวัด offset จะเพี้ยนแล้วเคอร์เซอร์ลอยเอง
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
    Serial.printf("[CALIBRATION] ถุงมือขยับระหว่างวัด (range=%.3f) ลองใหม่ครั้งที่ %u/%u...\n",
                  range, attempt, Config::CALIB_MAX_ATTEMPTS);
  }
  Serial.printf("[CALIBRATION] ⚠️ วัดไม่นิ่ง ใช้ค่าล่าสุด: X=%.4f, Y=%.4f, Z=%.4f (เคอร์เซอร์อาจลอย ลองรีเซ็ตบอร์ดตอนวางนิ่ง)\n",
                offsetX, offsetY, offsetZ);
}

bool MPU6050Driver::readGyro(float &gx, float &gy, float &gz) const {
  Wire.beginTransmission(activeAddress);
  Wire.write(REG_GYRO_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(activeAddress, (uint8_t)6) != 6) return false;

  int16_t rawX = (Wire.read() << 8) | Wire.read();
  int16_t rawY = (Wire.read() << 8) | Wire.read();
  int16_t rawZ = (Wire.read() << 8) | Wire.read();

  // แปลงค่า LSB เป็น rad/s (สเกล +-500 deg/s คือ 65.5 LSB/deg/s)
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

uint8_t MPU6050Driver::readRegister(uint8_t reg) const {
  Wire.beginTransmission(activeAddress);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(activeAddress, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}
