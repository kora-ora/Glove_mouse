#ifndef MOUSE_TYPES_H
#define MOUSE_TYPES_H

#include <Arduino.h>

// โครงสร้างข้อมูลที่ส่งจาก Sensor Task ไปยัง BLE Task ผ่าน FreeRTOS Queue
struct MousePacket {
  int8_t  dx;       // ระยะเคลื่อนที่แกน X (-127 ถึง 127)
  int8_t  dy;       // ระยะเคลื่อนที่แกน Y (-127 ถึง 127)
  uint8_t buttons;  // บิตแสดงสถานะปุ่มคลิกปัจจุบัน (bit 0 = ซ้าย, bit 1 = ขวา) มาจาก Flex Sensor
};

#endif // MOUSE_TYPES_H
