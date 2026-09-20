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
  // ไลบรารี Arduino ใช้ I2C address แบบ 7-bit: 0x3C/0x3D
  // บาง datasheet ระบุเป็น 8-bit write address: 0x78/0x7A ตามลำดับ
  constexpr uint8_t  OLED_I2C_ADDR      = 0x3C;     // 0x78 แบบ 8-bit
  constexpr uint8_t  OLED_BACKUP_ADDR   = 0x3D;     // 0x7A แบบ 8-bit
  constexpr uint8_t  OLED_WIDTH         = 128;
  constexpr uint8_t  OLED_HEIGHT        = 64;
  constexpr uint32_t OLED_REFRESH_MS    = 200;      // ลดการเขียน I2C ซ้ำโดยไม่จำเป็น

  // ==========================================
  // 2. การปรับแต่งความไวและการเคลื่อนที่ของเมาส์
  // ==========================================
  constexpr uint16_t SENSOR_SAMPLE_RATE_HZ = 100;  // MPU6050 sample rate (เดิมเป็น 1 kHz เพราะไม่ได้ตั้ง SMPLRT_DIV)
  constexpr float    SENSITIVITY_X      = 50.0f;  // ความไวแกน X (ซ้าย-ขวา) ตอน 100 Hz (rate x sensitivity = 5000)
  constexpr float    SENSITIVITY_Y      = 50.0f;  // ความไวแกน Y (ขึ้น-ลง)
  constexpr float    DEADZONE           = 0.06f;   // ตัดสัญญาณมือสั่น (rad/s)
  constexpr bool     INVERT_X           = false;   // สลับทิศทางแนวนอน
  constexpr bool     INVERT_Y           = true;    // สลับทิศทางแนวตั้ง (Pitch)

  // ==========================================
  // 3. การ Calibration
  // ==========================================
  constexpr uint16_t CALIB_SAMPLES       = 200;    // จำนวนรอบอ่านค่าเฉลี่ย
  constexpr uint16_t CALIB_DELAY_MS      = 5;      // หน่วงเวลาระหว่างรอบ
  constexpr float    CALIB_MAX_RANGE     = 0.15f;  // ช่วงกว้างสูงสุดของ gyro ระหว่างวัด (rad/s) เกินนี้ถือว่าขยับ
  constexpr uint8_t  CALIB_MAX_ATTEMPTS  = 5;      // จำนวนครั้งที่วัดซ้ำสูงสุด

  // ==========================================
  // 4. FreeRTOS Tasks และ คิวข้อมูล (Queue)
  // ==========================================
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
  constexpr uint8_t  MAX_HOSTS             = 2;
  // ความเสถียรของ BLE
  // Connection interval (หน่วย 1.25 ms): 12-24 = 15-30 ms เหมาะกับการต่อ 2 เครื่องพร้อมกัน (7.5 ms ทำให้ 2 ลิงก์แย่งเวลาวิทยุกัน)
  // ขอปรับก็ต่อเมื่อเครื่องที่ต่อเลือก interval ช้ากว่า MAX เท่านั้น และขอหลังต่อเสร็จ BLE_PARAM_CHECK_DELAY_MS
  constexpr uint16_t BLE_CONN_INTERVAL_MIN = 12;
  constexpr uint16_t BLE_CONN_INTERVAL_MAX = 24;
  constexpr uint16_t BLE_CONN_TIMEOUT      = 400;   // supervision timeout หน่วย 10 ms = 4 วินาที (ทนสัญญาณหายชั่วคราว)
  constexpr uint32_t BLE_PARAM_CHECK_DELAY_MS = 2000;
  constexpr uint32_t BLE_ADV_CHECK_MS      = 1000;  // ตรวจว่ายัง advertise อยู่ตราบใดที่มีช่องว่าง
  constexpr uint16_t BLE_ADV_INTERVAL_MIN  = 80;    // หน่วย 0.625 ms = 50 ms
  constexpr uint16_t BLE_ADV_INTERVAL_MAX  = 160;   // 100 ms
  constexpr int8_t   BLE_TX_POWER_DBM      = 9;     // กำลังส่งสูงสุดของ ESP32 (+9 dBm)
  constexpr uint16_t HID_APPEARANCE_MOUSE  = 0x03C2;

  // ==========================================
  // 6.1 Capacitive Touch (แตะ 1 ครั้ง = สลับ ทำงาน/หยุด, แตะ 2 ครั้ง = สลับเครื่อง A<->B)
  // ==========================================
  constexpr uint8_t  PIN_TOUCH             = 27;   // GPIO 27 = T7 (ห้ามใช้ GPIO 4 เพราะเป็นขา INT ของ MPU6050)
  constexpr uint32_t TOUCH_THRESHOLD       = 500;  // touchRead < ค่านี้ = แตะ (ค่าต่ำลงเมื่อแตะ)
  constexpr uint8_t  TOUCH_DEBOUNCE_SAMPLES = 2;   // ต้องอ่านได้สถานะเดิมติดกันกี่ครั้งถึงเชื่อ
  constexpr uint32_t TOUCH_SAMPLE_MS       = 10;   // ตรวจค่าทัชทุกกี่ ms
  constexpr uint32_t TOUCH_TAP_WINDOW_MS   = 400;  // แตะซ้ำภายในเวลานี้นับเป็นชุดเดียว (แตะครั้งเดียวจึงตอบสนองหลังเวลานี้)

  // ==========================================
  // 7. Clipboard Service (custom GATT: เครื่อง <-> ESP32)
  // ==========================================
  constexpr size_t   CLIP_MAX_BYTES        = 16384;   // ข้อความใหญ่กว่านี้ถูกปฏิเสธ
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
