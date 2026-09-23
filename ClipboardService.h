#ifndef CLIPBOARD_SERVICE_H
#define CLIPBOARD_SERVICE_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "ClipboardStore.h"

// Custom GATT Service สำหรับ Clipboard sync ผ่าน ESP32
class ClipboardService : public NimBLECharacteristicCallbacks {
public:
  void begin(NimBLEServer *server);
  void service();
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &connInfo) override;

private:
  enum PacketType : uint8_t {
    PKT_START = 0x01, PKT_DATA = 0x02, PKT_END = 0x03,
    PKT_ACK = 0x10, PKT_NACK = 0x11,
    PKT_GET = 0x20, PKT_CLEAR = 0x21,
  };
  enum TxStage : uint8_t { TX_START, TX_DATA, TX_END };

  struct Tx {
    bool     active = false;
    uint16_t connHandle = 0;
    uint8_t  msgId = 0;
    TxStage  stage = TX_START;
    uint16_t chunk = Config::CLIP_MIN_CHUNK;
    uint16_t total = 0;
    uint32_t crc = 0;
    uint16_t offset = 0;
    uint16_t seq = 0;
    uint32_t startMs = 0;
  };

  void sendControl(uint16_t connHandle, PacketType type, uint8_t msgId, ClipErr err = CLIP_OK);
  void updateStatus();
  void startGet(uint16_t connHandle, uint16_t mtu, uint8_t msgId);

  NimBLEServer         *_server = nullptr;
  NimBLECharacteristic *_tx = nullptr;
  NimBLECharacteristic *_status = nullptr;
  ClipboardStore        _store;
  SemaphoreHandle_t     _txMutex = nullptr;
  Tx                    _send;
};

#endif // CLIPBOARD_SERVICE_H
