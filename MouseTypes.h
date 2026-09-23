#ifndef MOUSE_TYPES_H
#define MOUSE_TYPES_H

#include <Arduino.h>

// Bitmask ของปุ่มคลิก (bit 0 = ซ้าย, bit 1 = ขวา)
constexpr uint8_t MOUSE_BUTTON_LEFT  = 0b01;
constexpr uint8_t MOUSE_BUTTON_RIGHT = 0b10;

// โครงสร้างข้อมูลที่ส่งจาก Sensor Task ไปยัง BLE Task ผ่าน FreeRTOS Queue
struct MousePacket {
  int8_t  dx;       // ระยะเคลื่อนที่แกน X (-127 ถึง 127)
  int8_t  dy;       // ระยะเคลื่อนที่แกน Y (-127 ถึง 127)
  uint8_t buttons;  // บิตแสดงสถานะปุ่มคลิกปัจจุบัน (bit 0 = ซ้าย, bit 1 = ขวา) มาจาก Capacitive Touch
};

#endif // MOUSE_TYPES_H
