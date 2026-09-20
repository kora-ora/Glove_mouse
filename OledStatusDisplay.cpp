#include "OledStatusDisplay.h"

#include "FlexClickManager.h"

bool OledStatusDisplay::begin(const Ds3231Rtc *rtc) {
  _rtc = rtc;
  _address = Config::OLED_I2C_ADDR;
  _available = _display.begin(_address, true);
  if (!_available) {
    _address = Config::OLED_BACKUP_ADDR;
    _available = _display.begin(_address, true);
  }
  if (!_available) {
    Serial.println("⚠️ [OLED] ไม่พบ SH1106 ที่ I2C address 0x3C/0x3D");
    return false;
  }

  Serial.printf("✅ [OLED] SH1106 พร้อมใช้งานที่ I2C address 0x%02X\n", _address);
  _display.clearDisplay();
  _display.setTextColor(SH110X_WHITE);
  _display.display();
  return true;
}

void OledStatusDisplay::showCalibrating() {
  if (!_available) return;

  _display.clearDisplay();
  _display.setTextSize(1);
  _display.setCursor(18, 22);
  _display.println("Calibrating...");
  _display.setCursor(0, 40);
  _display.println("Keep glove still");
  _display.display();
}

void OledStatusDisplay::drawButtons(uint8_t buttons) {
  _display.print("Click: ");
  _display.print((buttons & FLEX_BUTTON_LEFT) != 0 ? "L" : "-");
  _display.print(" ");
  _display.println((buttons & FLEX_BUTTON_RIGHT) != 0 ? "R" : "-");
}

void OledStatusDisplay::clear() {
  if (!_available) return;
  _display.clearDisplay();
  _display.display();
}

void OledStatusDisplay::update(bool connected, int activeSlot, const MousePacket &packet) {
  if (!_available || millis() - _lastRefreshAt < Config::OLED_REFRESH_MS) return;
  _lastRefreshAt = millis();

  _display.clearDisplay();
  _display.setTextSize(1);
  _display.setCursor(0, 16);
  if (_rtc != nullptr) {
    char time[9];
    _display.print("Time: ");
    // อ่านเวลาไม่ได้ (ไม่พบ RTC / address ผิด / ค่าไม่ถูกต้อง) ให้แสดงขีดแทน จะได้รู้ว่าอ่านไม่ได้ ไม่ใช่ไม่วาด
    _display.println(_rtc->readTime(time, sizeof(time)) ? time : "--:--:--");
  }
  _display.setCursor(0, 30);
  if (connected && activeSlot >= 0) {
    _display.print("BLE: Connected ");
    _display.println(static_cast<char>('A' + activeSlot));
  } else {
    _display.println("BLE: Waiting...");
  }
  _display.setCursor(0, 44);
  _display.printf("Move X:%+d Y:%+d\n", packet.dx, packet.dy);
  _display.setCursor(0, 56);
  drawButtons(packet.buttons);
  _display.display();
}
