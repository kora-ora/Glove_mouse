#ifndef HID_MOUSE_SERVICE_H
#define HID_MOUSE_SERVICE_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "Config.h"
#include "MouseTypes.h"

class HidMouseService : public NimBLEServerCallbacks {
public:
  void begin(const char *deviceName, void (*beforeStart)(NimBLEServer *) = nullptr);

  bool isConnected() const;
  bool send(const MousePacket &packet);
  bool switchHost();
  void pause();
  void resume();

  bool isPaused() const   { return _paused; }
  int  activeSlot() const { return _active; }

  void service();

  // NimBLEServerCallbacks
  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override;
  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override;
  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override;

private:
  static constexpr uint16_t NO_CONN = BLE_HS_CONN_HANDLE_NONE;

  // รวบตัวแปรสถานะของแต่ละ Slot เข้าโครงสร้างเดียว
  struct HostSlot {
    volatile uint16_t handle        = NO_CONN;
    uint32_t          connectedAtMs = 0;
    uint32_t          lastTxMs      = 0;
    bool              paramsChecked = true;

    bool isConnected() const { return handle != NO_CONN; }
    void reset() {
      handle        = NO_CONN;
      connectedAtMs = 0;
      lastTxMs      = 0;
      paramsChecked = true;
    }
  };

  bool sendReport(uint16_t connHandle, uint8_t buttons, int8_t dx, int8_t dy);
  int  findFreeSlot() const;
  int  pickConnectedSlot(int preferred = -1) const;
  int  slotOfHandle(uint16_t connHandle) const;
  void restartAdvertising();

  // Background Workers ย่อย
  void serviceKeepAlive(uint32_t now);
  void serviceAdvertisingCheck(uint32_t now);
  void serviceConnectionParams(uint32_t now);

  NimBLEServer          *_server = nullptr;
  NimBLECharacteristic *_input  = nullptr;

  HostSlot _slots[Config::MAX_HOSTS];

  uint32_t      _lastAdvCheckMs = 0;
  uint8_t       _lastButtons    = 0;
  volatile int  _active         = 0;
  volatile bool _paused         = false;
};

#endif // HID_MOUSE_SERVICE_H
