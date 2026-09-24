#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

namespace Config {
  // 1. ฮาร์ดแวร์และ I2C
  constexpr uint8_t  PIN_SDA            = 21;
  constexpr uint8_t  PIN_SCL            = 22;
  constexpr uint32_t I2C_CLOCK_SPEED    = 400000;
  constexpr uint8_t  MPU_DEFAULT_ADDR   = 0x69;
  constexpr uint8_t  RTC_I2C_ADDR       = 0x68;
  constexpr uint8_t  INTERRUPT_PIN      = 4;
  constexpr uint8_t  OLED_I2C_ADDR      = 0x3C;
  constexpr uint8_t  OLED_BACKUP_ADDR   = 0x3D;
  constexpr uint8_t  OLED_WIDTH         = 128;
  constexpr uint8_t  OLED_HEIGHT        = 64;
  constexpr uint32_t OLED_REFRESH_MS    = 200;

  // Buzzer
  constexpr uint8_t  PIN_BUZZER          = 26;
  constexpr bool     BUZZER_ACTIVE_HIGH  = true;
  constexpr uint16_t BUZZER_SWITCH_MS    = 100;
  constexpr uint16_t BUZZER_TAP_MS       = 40;

  // 2. เมาส์และความไว
  constexpr uint16_t SENSOR_SAMPLE_RATE_HZ = 500;
  constexpr float    SENSITIVITY_X      = 25.0f;
  constexpr float    SENSITIVITY_Y      = 25.0f;
  constexpr float    DEADZONE           = 0.06f;
  constexpr bool     INVERT_X           = false;
  constexpr bool     INVERT_Y           = true;

  // 3. Calibration
  constexpr uint16_t CALIB_SAMPLES       = 200;
  constexpr uint16_t CALIB_DELAY_MS      = 5;
  constexpr float    CALIB_MAX_RANGE     = 0.15f;
  constexpr uint8_t  CALIB_MAX_ATTEMPTS  = 5;

  // 4. FreeRTOS Tasks
  constexpr UBaseType_t QUEUE_LENGTH       = 10;
  constexpr uint32_t    STACK_SENSOR_TASK  = 4096;
  constexpr uint32_t    STACK_BLE_TASK     = 4096;
  constexpr UBaseType_t PRIORITY_SENSOR    = 2;
  constexpr UBaseType_t PRIORITY_BLE       = 1;
  constexpr BaseType_t  CORE_SENSOR_TASK   = 1;
  constexpr BaseType_t  CORE_BLE_TASK      = 0;

  // 5. Capacitive Touch Click
  constexpr uint8_t  PIN_TOUCH_INDEX            = 33;   // GPIO 33 = T8 (คลิกซ้าย - นิ้วชี้)
  constexpr uint8_t  PIN_TOUCH_MIDDLE           = 32;   // GPIO 32 = T9 (คลิกขวา - นิ้วกลาง)
  constexpr uint32_t TOUCH_CLICK_THRESHOLD      = 500;  // touchRead < ค่านี้ = แตะ (เหมือนตัวเปลี่ยน state)
  constexpr uint8_t  TOUCH_CLICK_DEBOUNCE_SAMPLES = 2;  // จำนวน sample สำหรับ debounce กันสัญญาณรบกวน

  // 5.1 Legacy Flex Sensor (รองรับไฟล์เดิมหากเปิดแท็บค้างใน Arduino IDE)
  constexpr uint8_t  PIN_FLEX_INDEX        = 34;
  constexpr uint8_t  PIN_FLEX_MIDDLE       = 35;
  constexpr uint16_t FLEX_CALIB_SAMPLES    = 200;
  constexpr uint16_t FLEX_CALIB_DELAY_MS   = 10;
  constexpr int      FLEX_PRESS_MARGIN     = 300;
  constexpr int      FLEX_RELEASE_MARGIN   = 150;
  constexpr float    FLEX_FILTER_ALPHA     = 0.3f;
  constexpr uint8_t  FLEX_DEBOUNCE_SAMPLES = 3;
  constexpr int      FLEX_CALIB_MAX_RANGE  = 150;

  // 6. BLE Multi-Host
  constexpr uint8_t  MAX_HOSTS             = 2;
  constexpr uint16_t BLE_CONN_INTERVAL_MIN = 16;   // 20 ms
  constexpr uint16_t BLE_CONN_INTERVAL_MAX = 32;   // 40 ms
  constexpr uint16_t BLE_CONN_LATENCY      = 4;    // Slave Latency ให้เครื่อง inactive ข้ามรอบได้
  constexpr uint16_t BLE_CONN_TIMEOUT      = 400;  // 4 วินาที
  constexpr uint32_t BLE_PARAM_CHECK_DELAY_MS = 2000;
  constexpr uint32_t BLE_ADV_CHECK_MS      = 2500; // ตรวจ advertise เมื่อมี slot ว่าง
  constexpr uint32_t HID_KEEPALIVE_MS      = 5000;
  constexpr uint16_t BLE_ADV_INTERVAL_MIN  = 80;   // 50 ms
  constexpr uint16_t BLE_ADV_INTERVAL_MAX  = 160;  // 100 ms
  constexpr int8_t   BLE_TX_POWER_DBM      = 9;
  constexpr uint16_t HID_APPEARANCE_MOUSE  = 0x03C2;

  // 6.1 Capacitive Touch
  constexpr uint8_t  PIN_TOUCH             = 27;
  constexpr uint32_t TOUCH_THRESHOLD       = 500;
  constexpr uint8_t  TOUCH_DEBOUNCE_SAMPLES = 2;
  constexpr uint32_t TOUCH_SAMPLE_MS       = 10;
  constexpr uint32_t TOUCH_TAP_WINDOW_MS   = 400;
  constexpr uint32_t TOUCH_ISR_WAKE_HINT_MS = 50;

  // 6.2 Idle Mode
  constexpr uint32_t IDLE_TIMEOUT_MS       = 30000;

  // 6.3 I2C Reliability
  constexpr uint8_t  I2C_FAIL_THRESHOLD     = 10;
  constexpr uint8_t  I2C_RESTART_THRESHOLD  = 10;

  // 7. Clipboard Service (GATT)
  constexpr size_t   CLIP_MAX_BYTES        = 16384;
  constexpr uint32_t CLIP_TTL_MS           = 60000;
  constexpr uint32_t CLIP_RX_TIMEOUT_MS    = 5000;
  constexpr uint16_t CLIP_MIN_CHUNK        = 16;
  constexpr uint16_t CLIP_MAX_CHUNK        = 240;
  constexpr const char *CLIP_SERVICE_UUID  = "7d3c0001-9a4e-4f6b-8c21-5b6e1f0a9d10";
  constexpr const char *CLIP_RX_UUID       = "7d3c0002-9a4e-4f6b-8c21-5b6e1f0a9d10";
  constexpr const char *CLIP_TX_UUID       = "7d3c0003-9a4e-4f6b-8c21-5b6e1f0a9d10";
  constexpr const char *CLIP_STATUS_UUID   = "7d3c0004-9a4e-4f6b-8c21-5b6e1f0a9d10";
}

#endif // CONFIG_H
