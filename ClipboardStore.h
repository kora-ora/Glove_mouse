#ifndef CLIPBOARD_STORE_H
#define CLIPBOARD_STORE_H

#include <Arduino.h>
#include "Config.h"

// รหัส error ที่ใช้ทั้งใน Store และ NACK ของโปรโตคอล
enum ClipErr : uint8_t {
  CLIP_OK        = 0,
  CLIP_TOO_LARGE = 1,
  CLIP_BAD_SEQ   = 2,
  CLIP_BAD_CRC   = 3,
  CLIP_TIMEOUT   = 4,
  CLIP_BAD_STATE = 5,
  CLIP_EMPTY     = 6,
};

enum ClipState : uint8_t { CLIP_STATE_EMPTY = 0, CLIP_STATE_HAS = 1, CLIP_STATE_RECEIVING = 2 };

uint32_t clipCrc32(const uint8_t *data, size_t len, uint32_t crc = 0);  // CRC-32 มาตรฐาน (เท่ากับ zlib.crc32)

// เก็บข้อความ 1 ก้อนในแรม (ไม่รู้จัก BLE) ปลอดภัยต่อการเรียกจากหลาย Task
// ล้างด้วย memset เสมอ ไม่ใช่แค่ตั้งความยาวเป็น 0
class ClipboardStore {
public:
  void begin();

  ClipErr beginWrite(uint8_t msgId, uint16_t totalLen, uint32_t crc);  // ทิ้งของเก่า เริ่มรับก้อนใหม่
  ClipErr append(uint8_t msgId, uint16_t seq, const uint8_t *data, size_t len);
  ClipErr commit(uint8_t msgId);                                       // ตรวจความยาว + CRC แล้วเริ่มนับ TTL

  size_t    copyOut(size_t offset, uint8_t *dst, size_t maxLen);       // คืนจำนวนไบต์ที่คัดลอกได้
  uint16_t  size();
  uint32_t  crc();
  ClipState state();
  void      clear();

  // เรียกเป็นระยะ: ล้างเมื่อ TTL หมด / รับค้างเกินเวลา. คืน true ถ้าสถานะเปลี่ยน
  bool expireIfNeeded();

private:
  SemaphoreHandle_t _mutex = nullptr;
  uint8_t  _buf[Config::CLIP_MAX_BYTES];
  uint16_t _len = 0;         // ความยาวที่รับแล้ว
  uint16_t _expectedLen = 0;
  uint32_t _expectedCrc = 0;
  uint16_t _nextSeq = 0;
  uint8_t  _msgId = 0;
  ClipState _state = CLIP_STATE_EMPTY;
  uint32_t _stamp = 0;       // เวลาเริ่มรับ/รับสำเร็จล่าสุด (ms)

  void clearLocked();
};

#endif // CLIPBOARD_STORE_H
