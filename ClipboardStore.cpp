#include "ClipboardStore.h"
#include <algorithm>

// ==============================================================================
// 1. RAII Mutex Helper (ปลดล็อกอัตโนมัติเมื่อจบสโคป หมดปัญหา Mutex ค้าง)
// ==============================================================================
namespace {
class MutexLock {
public:
  explicit MutexLock(SemaphoreHandle_t mutex) : _mutex(mutex) {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
  }
  ~MutexLock() {
    if (_mutex) xSemaphoreGive(_mutex);
  }
  MutexLock(const MutexLock &) = delete;
  MutexLock &operator=(const MutexLock &) = delete;

private:
  SemaphoreHandle_t _mutex;
};
} // namespace

// ==============================================================================
// 2. Checksum Calculator
// ==============================================================================
uint32_t clipCrc32(const uint8_t *data, size_t len, uint32_t crc) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

// ==============================================================================
// 3. Lifecycle & Clear Methods
// ==============================================================================
void ClipboardStore::begin() {
  _mutex = xSemaphoreCreateMutex();
  clear();
}

void ClipboardStore::clearLocked() {
  memset(_buf, 0, sizeof(_buf));
  _len = _expectedLen = _nextSeq = 0;
  _expectedCrc = 0;
  _state = CLIP_STATE_EMPTY;
}

void ClipboardStore::clear() {
  MutexLock lock(_mutex);
  clearLocked();
}

// ==============================================================================
// 4. Data Writing & Validation (ใช้ Early Exit ลด Indentation)
// ==============================================================================
ClipErr ClipboardStore::beginWrite(uint8_t msgId, uint16_t totalLen, uint32_t crc) {
  MutexLock lock(_mutex);
  clearLocked(); // ล้างของเก่าทิ้งทันทีเสมอ

  if (totalLen == 0 || totalLen > Config::CLIP_MAX_BYTES) {
    return CLIP_TOO_LARGE;
  }

  _msgId       = msgId;
  _expectedLen = totalLen;
  _expectedCrc = crc;
  _state       = CLIP_STATE_RECEIVING;
  _stamp       = millis();

  return CLIP_OK;
}

ClipErr ClipboardStore::append(uint8_t msgId, uint16_t seq, const uint8_t *data, size_t len) {
  MutexLock lock(_mutex);

  // ตรวจสอบความถูกต้องแบบ Guard Clauses ทีละเงื่อนไข
  if (_state != CLIP_STATE_RECEIVING || msgId != _msgId) {
    return CLIP_BAD_STATE;
  }
  if (seq != _nextSeq) {
    clearLocked();
    return CLIP_BAD_SEQ;
  }
  if (_len + len > _expectedLen) {
    clearLocked();
    return CLIP_TOO_LARGE;
  }

  // นำข้อมูลเข้า Buffer
  memcpy(_buf + _len, data, len);
  _len     += len;
  _nextSeq++;
  _stamp   = millis();

  return CLIP_OK;
}

ClipErr ClipboardStore::commit(uint8_t msgId) {
  MutexLock lock(_mutex);

  if (_state != CLIP_STATE_RECEIVING || msgId != _msgId) {
    return CLIP_BAD_STATE;
  }
  if (_len != _expectedLen) {
    clearLocked();
    return CLIP_BAD_SEQ;
  }
  if (clipCrc32(_buf, _len) != _expectedCrc) {
    clearLocked();
    return CLIP_BAD_CRC;
  }

  _state = CLIP_STATE_HAS;
  _stamp = millis();
  return CLIP_OK;
}

// ==============================================================================
// 5. Data Reading & Getters
// ==============================================================================
size_t ClipboardStore::copyOut(size_t offset, uint8_t *dst, size_t maxLen) {
  MutexLock lock(_mutex);

  if (_state != CLIP_STATE_HAS || offset >= _len) {
    return 0;
  }

  size_t bytesToCopy = std::min(maxLen, static_cast<size_t>(_len - offset));
  memcpy(dst, _buf + offset, bytesToCopy);
  return bytesToCopy;
}

uint16_t ClipboardStore::size() {
  MutexLock lock(_mutex);
  return (_state == CLIP_STATE_HAS) ? _len : 0;
}

uint32_t ClipboardStore::crc() {
  MutexLock lock(_mutex);
  return _expectedCrc;
}

ClipState ClipboardStore::state() {
  MutexLock lock(_mutex);
  return _state;
}

bool ClipboardStore::expireIfNeeded() {
  MutexLock lock(_mutex);

  uint32_t age = millis() - _stamp;
  bool isCompletedExpired = (_state == CLIP_STATE_HAS && age >= Config::CLIP_TTL_MS);
  bool isReceivingTimeout = (_state == CLIP_STATE_RECEIVING && age >= Config::CLIP_RX_TIMEOUT_MS);

  if (isCompletedExpired || isReceivingTimeout) {
    clearLocked();
    return true;
  }

  return false;
}
