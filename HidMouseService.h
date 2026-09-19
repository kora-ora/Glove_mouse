#ifndef HID_MOUSE_SERVICE_H
#define HID_MOUSE_SERVICE_H

#include <Arduino.h>
#include <NimBLEDevice.h> 
#include "Config.h"
#include "MouseTypes.h"

// BLE HID Mouse บน NimBLE ที่ให้ host ต่อพร้อมกันได้ Config::MAX_HOSTS เครื่อง
// ส่ง report ไปเฉพาะ host ที่ active (เรียก send()/switchHost() จาก Task เดียวเท่านั้น)
class HidMouseService : public NimBLEServerCallbacks {
public:
  // beforeStart: เรียกก่อน server->start() ให้ service อื่นๆ (เช่น Clipboard) ผูกเข้า server เดียวกัน
  void begin(const char *deviceName, void (*beforeStart)(NimBLEServer *) = nullptr);

  bool isConnected() const;                 // มี host ต่ออยู่อย่างน้อย 1 เครื่อง
  bool send(const MousePacket &packet);     // ส่งไป host ที่ active (ถ้า active หลุดจะย้ายไปเครื่องอื่นเอง)
  bool switchHost();                        // ปล่อยปุ่มบนเครื่องเดิม แล้วสลับไปเครื่องถัดไปที่ต่ออยู่
  void pause();                             // หยุดส่งข้อมูลเมาส์ (ปล่อยปุ่มที่ค้างบนเครื่อง active ก่อน)
  void resume();                            // กลับมาส่งต่อที่เครื่อง active เดิม
  bool isPaused() const { return _paused; }
  int  activeSlot() const { return _active; }

  // เรียกเป็นระยะจาก Task เดียว (TaskBleMouse): ดูแลให้ยัง advertise อยู่ และตรวจ/ปรับ connection params
  void service();

  // NimBLEServerCallbacks (รันใน NimBLE host task)
  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override;
  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override;
  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override;

private:
  static constexpr uint16_t NO_CONN = BLE_HS_CONN_HANDLE_NONE;

  bool sendReport(uint16_t connHandle, uint8_t buttons, int8_t dx, int8_t dy);
  int  pickConnectedSlot(int preferred) const;
  int  slotOfHandle(uint16_t connHandle) const;
  void restartAdvertising();

  NimBLEServer         *_server = nullptr;
  NimBLECharacteristic *_input  = nullptr;

  volatile uint16_t _handle[Config::MAX_HOSTS];  // conn handle ของแต่ละ slot (NO_CONN = ว่าง)
  NimBLEAddress     _addr[Config::MAX_HOSTS];    // address ล่าสุดของแต่ละ slot ให้เครื่องเดิมได้ slot เดิม
  bool              _hasAddr[Config::MAX_HOSTS] = {false, false};
  volatile uint32_t _connectedAtMs[Config::MAX_HOSTS] = {0, 0};
  volatile bool     _paramsChecked[Config::MAX_HOSTS] = {true, true};
  uint32_t          _lastAdvCheckMs = 0;
  uint32_t          _lastAdvLogMs = 0;
  volatile int      _active = 0;
  volatile bool     _paused = false;
};

#endif // HID_MOUSE_SERVICE_H
