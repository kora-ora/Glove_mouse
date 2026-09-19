#include "ClipboardStore.h"

uint32_t clipCrc32(const uint8_t *data, size_t len, uint32_t crc) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

void ClipboardStore::begin() {
  _mutex = xSemaphoreCreateMutex();
  clearLocked();
}

void ClipboardStore::clearLocked() {
  memset(_buf, 0, sizeof(_buf));
  _len = _expectedLen = _nextSeq = 0;
  _expectedCrc = 0;
  _state = CLIP_STATE_EMPTY;
}

void ClipboardStore::clear() {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  clearLocked();
  xSemaphoreGive(_mutex);
}

ClipErr ClipboardStore::beginWrite(uint8_t msgId, uint16_t totalLen, uint32_t crc) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  clearLocked();  // ของเก่าถูกทิ้งเสมอ แม้รอบนี้จะล้มเหลว
  ClipErr err = CLIP_OK;
  if (totalLen == 0 || totalLen > Config::CLIP_MAX_BYTES) {
    err = CLIP_TOO_LARGE;
  } else {
    _msgId = msgId;
    _expectedLen = totalLen;
    _expectedCrc = crc;
    _state = CLIP_STATE_RECEIVING;
    _stamp = millis();
  }
  xSemaphoreGive(_mutex);
  return err;
}

ClipErr ClipboardStore::append(uint8_t msgId, uint16_t seq, const uint8_t *data, size_t len) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  ClipErr err = CLIP_OK;
  if (_state != CLIP_STATE_RECEIVING || msgId != _msgId) {
    err = CLIP_BAD_STATE;
  } else if (seq != _nextSeq) {
    err = CLIP_BAD_SEQ;
  } else if (_len + len > _expectedLen) {
    err = CLIP_TOO_LARGE;
  } else {
    memcpy(_buf + _len, data, len);
    _len += len;
    _nextSeq++;
    _stamp = millis();
  }
  if (err != CLIP_OK && _state == CLIP_STATE_RECEIVING && msgId == _msgId) clearLocked();
  xSemaphoreGive(_mutex);
  return err;
}

ClipErr ClipboardStore::commit(uint8_t msgId) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  ClipErr err = CLIP_OK;
  if (_state != CLIP_STATE_RECEIVING || msgId != _msgId) {
    err = CLIP_BAD_STATE;
  } else if (_len != _expectedLen) {
    err = CLIP_BAD_SEQ;  // ชิ้นไม่ครบ
    clearLocked();
  } else if (clipCrc32(_buf, _len) != _expectedCrc) {
    err = CLIP_BAD_CRC;
    clearLocked();
  } else {
    _state = CLIP_STATE_HAS;
    _stamp = millis();
  }
  xSemaphoreGive(_mutex);
  return err;
}

size_t ClipboardStore::copyOut(size_t offset, uint8_t *dst, size_t maxLen) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  size_t n = 0;
  if (_state == CLIP_STATE_HAS && offset < _len) {
    n = min(maxLen, (size_t)(_len - offset));
    memcpy(dst, _buf + offset, n);
  }
  xSemaphoreGive(_mutex);
  return n;
}

uint16_t ClipboardStore::size() {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  uint16_t n = (_state == CLIP_STATE_HAS) ? _len : 0;
  xSemaphoreGive(_mutex);
  return n;
}

uint32_t ClipboardStore::crc() {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  uint32_t c = _expectedCrc;
  xSemaphoreGive(_mutex);
  return c;
}

ClipState ClipboardStore::state() {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  ClipState s = _state;
  xSemaphoreGive(_mutex);
  return s;
}

bool ClipboardStore::expireIfNeeded() {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  bool changed = false;
  uint32_t age = millis() - _stamp;
  if ((_state == CLIP_STATE_HAS && age >= Config::CLIP_TTL_MS) ||
      (_state == CLIP_STATE_RECEIVING && age >= Config::CLIP_RX_TIMEOUT_MS)) {
    clearLocked();
    changed = true;
  }
  xSemaphoreGive(_mutex);
  return changed;
}
