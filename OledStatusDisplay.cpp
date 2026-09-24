#include "OledStatusDisplay.h"

#include "TouchClickManager.h"

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
  _display.setRotation(2);  // จอต่อกลับหัวบนถุงมือ หมุนภาพ 180 องศาเพื่อแก้ (0=ปกติ, 2=กลับหัว)
  _display.clearDisplay();
  _display.setTextColor(SH110X_WHITE);
  _display.display();
  return true;
}

void OledStatusDisplay::showCalibrating() {
  if (!_available) return;

  _display.clearDisplay();
  _display.setTextColor(SH110X_WHITE);

  // Header
  _display.setTextSize(1);
  _display.setCursor(18, 2);
  _display.print("GLOVE AIR MOUSE");
  _display.drawFastHLine(0, 12, 128, SH110X_WHITE);

  // Calibrating badge
  _display.drawRoundRect(8, 18, 112, 22, 3, SH110X_WHITE);
  _display.setTextSize(1);
  _display.setCursor(10, 25);
  _display.print("CALIBRATING SENSOR");

  // Footer guide
  _display.drawFastHLine(0, 48, 128, SH110X_WHITE);
  _display.setCursor(16, 53);
  _display.print("Keep glove still");
  _display.display();
}

void OledStatusDisplay::clear() {
  if (!_available) return;
  _display.clearDisplay();
  _display.display();
}

void OledStatusDisplay::update(bool connected, int activeSlot, bool paused, const MousePacket &packet) {
  if (!_available) return;

  const uint32_t now = millis();
  const bool stateChanged = (connected != _lastConnected || activeSlot != _lastSlot || paused != _lastPaused);
  const bool isMoving = (packet.dx != 0 || packet.dy != 0);

  // ขณะที่เมาส์กำลังเคลื่อนไหว (isMoving): งดส่ง Framebuffer 1KB ผ่าน I2C (~25ms) ชั่วคราว
  // เพื่อไม่ให้ I2C ไปบล็อกลูป 100Hz ของ MPU6050 จนเคอร์เซอร์สะดุด/กระตุก (Micro-stuttering)
  // จอจะกลับมาอัปเดตเมื่อมือหยุดนิ่ง (dx=0, dy=0), มีสถานะเปลี่ยน หรือครบ 1 วินาที (เพื่อเดินเวลา RTC)
  if (isMoving && !stateChanged && (now - _lastRefreshAt < 1000)) {
    return;
  }

  if (!stateChanged && (now - _lastRefreshAt < Config::OLED_REFRESH_MS)) return;
  _lastRefreshAt = now;
  _lastConnected = connected;
  _lastSlot = activeSlot;
  _lastPaused = paused;

  _display.clearDisplay();
  _display.setTextColor(SH110X_WHITE);

  // --- 1. Top Bar: เวลา (RTC) & สถานะ Host ---
  _display.setTextSize(1);
  _display.setCursor(2, 2);
  if (_rtc != nullptr) {
    char timeStr[9];
    _display.print(_rtc->readTime(timeStr, sizeof(timeStr)) ? timeStr : "--:--:--");
  } else {
    _display.print("--:--:--");
  }

  // ด้านขวาบน: ระบุ Host ปัจจุบัน
  if (connected && activeSlot >= 0) {
    _display.setCursor(86, 2);
    _display.printf("HOST %c", 'A' + activeSlot);
  } else {
    _display.setCursor(80, 2);
    _display.print("NO LINK");
  }
  _display.drawFastHLine(0, 12, 128, SH110X_WHITE);

  // --- 2. Main State (ตรงกลาง): แสดงสถานะการทำงานสำหรับ User ---
  if (!connected) {
    // ยังไม่เชื่อมต่อ BLE
    _display.drawRoundRect(14, 16, 100, 20, 3, SH110X_WHITE);
    _display.setTextSize(2);
    _display.setCursor(22, 19);
    _display.print("PAIRING");
  } else if (paused) {
    // เชื่อมต่อแล้ว แต่ Pause การขยับเมาส์ไว้ (แตะ 1 ครั้ง)
    _display.drawRoundRect(16, 16, 96, 20, 3, SH110X_WHITE);
    _display.setTextSize(2);
    _display.setCursor(28, 19);
    _display.print("PAUSED");
  } else {
    // เชื่อมต่อแล้ว พร้อมใช้งาน (Tracking Active)
    _display.fillRoundRect(16, 16, 96, 20, 3, SH110X_WHITE);
    _display.setTextColor(SH110X_BLACK);
    _display.setTextSize(2);
    _display.setCursor(28, 19);
    _display.print("ACTIVE");
    _display.setTextColor(SH110X_WHITE);
  }

  // --- 3. Action Hint: ท่าแตะ ตัวใหญ่บรรทัดเดียว (รวม status เดิม + hint เดิม) ---
  _display.setTextSize(2);
  if (!connected) {
    _display.setCursor(10, 44);
    _display.print("2x SWITCH");
  } else if (paused) {
    _display.setCursor(10, 44);
    _display.print("1x RESUME");
  } else {
    _display.setCursor(16, 44);
    _display.print("1x PAUSE");
  }

  _display.display();
}
