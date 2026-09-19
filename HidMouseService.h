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
  int  activeSlot() const { return _active; }

  // NimBLEServerCallbacks (รันใน NimBLE host task)
  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override;
  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override;

private:
  static constexpr uint16_t NO_CONN = BLE_HS_CONN_HANDLE_NONE;

  bool sendReport(uint16_t connHandle, uint8_t buttons, int8_t dx, int8_t dy);
  int  pickConnectedSlot(int preferred) const;
  void restartAdvertising();

  NimBLEServer         *_server = nullptr;
  NimBLECharacteristic *_input  = nullptr;

  volatile uint16_t _handle[Config::MAX_HOSTS];  // conn handle ของแต่ละ slot (NO_CONN = ว่าง)
  NimBLEAddress     _addr[Config::MAX_HOSTS];    // address ล่าสุดของแต่ละ slot ให้เครื่องเดิมได้ slot เดิม
  bool              _hasAddr[Config::MAX_HOSTS] = {false, false};
  volatile int      _active = 0;
};

#endif // HID_MOUSE_SERVICE_H
