#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

namespace Config {
  // ==========================================
  // 1. ฮาร์ดแวร์และการเชื่อมต่อ I2C
  // ==========================================
  constexpr uint8_t  PIN_SDA            = 21;
  constexpr uint8_t  PIN_SCL            = 22;
  constexpr uint32_t I2C_CLOCK_SPEED    = 400000;  // 400kHz Fast Mode
  constexpr uint8_t  MPU_DEFAULT_ADDR   = 0x68;
  constexpr uint8_t  MPU_BACKUP_ADDR    = 0x69;
  constexpr uint8_t  INTERRUPT_PIN      = 4;        // ขา INT ของ MPU6050 → GPIO 4

  // ==========================================
  // 2. การปรับแต่งความไวและการเคลื่อนที่ของเมาส์
  // ==========================================
  constexpr uint16_t SENSOR_SAMPLE_RATE_HZ = 100;  // MPU6050 sample rate (เดิมเป็น 1 kHz เพราะไม่ได้ตั้ง SMPLRT_DIV)
  constexpr float    SENSITIVITY_X      = 250.0f;  // ความไวแกน X (ซ้าย-ขวา) ตอน 100 Hz (เดิม 25 ที่ 1 kHz)
  constexpr float    SENSITIVITY_Y      = 250.0f;  // ความไวแกน Y (ขึ้น-ลง)
  constexpr float    DEADZONE           = 0.06f;   // ตัดสัญญาณมือสั่น (rad/s)
  constexpr bool     INVERT_X           = false;   // สลับทิศทางแนวนอน
  constexpr bool     INVERT_Y           = true;    // สลับทิศทางแนวตั้ง (Pitch)

  // ==========================================
  // 3. การ Calibration
  // ==========================================
  constexpr uint16_t CALIB_SAMPLES       = 200;    // จำนวนรอบอ่านค่าเฉลี่ย
  constexpr uint16_t CALIB_DELAY_MS      = 5;      // หน่วงเวลาระหว่างรอบ

  // ==========================================
  // 4. FreeRTOS Tasks และ คิวข้อมูล (Queue)
  // ==========================================
  constexpr uint32_t SENSOR_SAMPLE_RATE_MS = 10;   // อ่านเซนเซอร์ทุก 10ms (100 Hz) (ใช้เมื่อไม่ใช้ interrupt)
  constexpr UBaseType_t QUEUE_LENGTH       = 10;   // ขนาดบัฟเฟอร์ของคิวส่งข้อมูลเมาส์

  // Stack Sizes (หน่วยเป็น Words ใน ESP32 FreeRTOS, 1 word = 4 bytes)
  constexpr uint32_t STACK_SENSOR_TASK     = 4096;
  constexpr uint32_t STACK_BLE_TASK        = 4096;

  // Task Priorities (ยิ่งตัวเลขมาก Priority ยิ่งสูง)
  constexpr UBaseType_t PRIORITY_SENSOR    = 2;
  constexpr UBaseType_t PRIORITY_BLE       = 1;

  // Core ID (ESP32: Core 0 ดูแล WiFi/BT, Core 1 ดูแลงานทั่วไป)
  constexpr BaseType_t CORE_SENSOR_TASK    = 1;
  constexpr BaseType_t CORE_BLE_TASK       = 0;

  // ==========================================
  // 5. Flex Sensor (นิ้วชี้ = Left Click, นิ้วกลาง = Right Click)
  // ==========================================
  constexpr uint8_t  PIN_FLEX_INDEX        = 34;   // ADC1 เท่านั้น (ADC2 ชนกับ WiFi/BLE)
  constexpr uint8_t  PIN_FLEX_MIDDLE       = 35;
  constexpr uint16_t FLEX_CALIB_SAMPLES    = 200;  // baseline ครั้งเดียวตอน boot
  constexpr uint16_t FLEX_CALIB_DELAY_MS   = 10;
  constexpr int      FLEX_PRESS_MARGIN     = 300;  // ต้องปรับตามค่า ADC จริงจากเซนเซอร์
  constexpr int      FLEX_RELEASE_MARGIN   = 150;  // < PRESS_MARGIN กันสัญญาณกระตุก
  constexpr float    FLEX_FILTER_ALPHA     = 0.3f; // EMA กันสัญญาณแกว่ง
  constexpr uint8_t  FLEX_DEBOUNCE_SAMPLES = 3;    // 3 samples * 10ms = 30ms

  // ==========================================
  // 6. BLE Multi-Host (ต่อได้ 2 เครื่อง สลับด้วยปุ่ม)
  // ==========================================
  constexpr uint8_t  PIN_SWITCH            = 27;   // ปุ่มสลับเครื่อง ต่อลง GND (INPUT_PULLUP)
  constexpr uint32_t SWITCH_HOLD_MS        = 500;  // กดค้างนานเท่านี้ถึงสลับ
  constexpr uint8_t  MAX_HOSTS             = 2;
  constexpr uint16_t HID_APPEARANCE_MOUSE  = 0x03C2;

  // ==========================================
  // 7. Clipboard Service (custom GATT: เครื่อง <-> ESP32)
  // ==========================================
  constexpr size_t   CLIP_MAX_BYTES        = 4096;   // ข้อความใหญ่กว่านี้ถูกปฏิเสธ
  constexpr uint32_t CLIP_TTL_MS           = 60000;  // ล้างข้อความทิ้งเองหลังรับสำเร็จ
  constexpr uint32_t CLIP_RX_TIMEOUT_MS    = 5000;   // เงียบนานเท่านี้ระหว่างรับ = ทิ้งข้อความที่ค้าง
  constexpr uint16_t CLIP_MIN_CHUNK        = 16;     // ขนาด payload ต่อชิ้นขั้นต่ำ (MTU 23)
  constexpr uint16_t CLIP_MAX_CHUNK        = 240;
  constexpr const char *CLIP_SERVICE_UUID  = "7d3c0001-9a4e-4f6b-8c21-5b6e1f0a9d10";
  constexpr const char *CLIP_RX_UUID       = "7d3c0002-9a4e-4f6b-8c21-5b6e1f0a9d10";  // เครื่อง -> ESP32 (write)
  constexpr const char *CLIP_TX_UUID       = "7d3c0003-9a4e-4f6b-8c21-5b6e1f0a9d10";  // ESP32 -> เครื่อง (notify)
  constexpr const char *CLIP_STATUS_UUID   = "7d3c0004-9a4e-4f6b-8c21-5b6e1f0a9d10";  // สถานะ (read/notify)
}

#endif // CONFIG_H
