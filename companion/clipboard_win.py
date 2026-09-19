"""อ่าน/เขียนข้อความ clipboard บน Windows (ต้องมี pywin32)

- ดักการเปลี่ยนด้วย GetClipboardSequenceNumber (ไม่ต้อง hook)
- อ่านเฉพาะข้อความล้วน ข้ามไฟล์/รูป และข้อมูลที่โปรแกรมจัดการรหัสผ่านตั้งธงไม่ให้ยุ่ง
"""
import ctypes
import time

import win32clipboard as wc
import win32con

_user32 = ctypes.windll.user32
_user32.GetClipboardSequenceNumber.restype = ctypes.c_uint32

# ธงที่ password manager / Windows ใช้บอก "อย่าเก็บ/อย่า sync ข้อมูลนี้"
_FMT_EXCLUDE_MONITOR = wc.RegisterClipboardFormat("ExcludeClipboardContentFromMonitorProcessing")
_FMT_CAN_HISTORY = wc.RegisterClipboardFormat("CanIncludeInClipboardHistory")
_FMT_CAN_UPLOAD = wc.RegisterClipboardFormat("CanUploadToCloudClipboard")

_NON_TEXT_FORMATS = (win32con.CF_HDROP, win32con.CF_BITMAP, win32con.CF_DIB, win32con.CF_DIBV5)


def sequence_number() -> int:
    return _user32.GetClipboardSequenceNumber()


class _Clipboard:
    """เปิด clipboard พร้อม retry (โปรแกรมอื่นอาจถือไว้อยู่ชั่วครู่)"""

    def __enter__(self):
        for _ in range(20):
            try:
                wc.OpenClipboard()
                return self
            except Exception:
                time.sleep(0.025)
        raise OSError("เปิด clipboard ไม่ได้")

    def __exit__(self, *exc):
        wc.CloseClipboard()


def _flag_says_private() -> bool:
    if wc.IsClipboardFormatAvailable(_FMT_EXCLUDE_MONITOR):
        return True
    for fmt in (_FMT_CAN_HISTORY, _FMT_CAN_UPLOAD):
        if wc.IsClipboardFormatAvailable(fmt):
            try:
                raw = wc.GetClipboardData(fmt)
            except Exception:
                continue
            # ค่าเป็น DWORD: 0 = ห้าม
            if isinstance(raw, (bytes, bytearray)) and len(raw) >= 4 and int.from_bytes(raw[:4], "little") == 0:
                return True
    return False


def read_text():
    """คืนข้อความใน clipboard หรือ None ถ้าไม่ใช่ข้อความล้วน/เป็นข้อมูลส่วนตัว/ว่าง"""
    try:
        with _Clipboard():
            if not wc.IsClipboardFormatAvailable(win32con.CF_UNICODETEXT):
                return None
            if any(wc.IsClipboardFormatAvailable(f) for f in _NON_TEXT_FORMATS):
                return None
            if _flag_says_private():
                return None
            text = wc.GetClipboardData(win32con.CF_UNICODETEXT)
    except OSError:
        return None
    if not isinstance(text, str) or not text.strip():
        return None
    return text


def write_text(text: str) -> int:
    """ใส่ข้อความเข้า clipboard คืนหมายเลข sequence หลังเขียน (ใช้แยกว่าการเปลี่ยนนี้มาจากแอปเอง)"""
    with _Clipboard():
        wc.EmptyClipboard()
        wc.SetClipboardData(win32con.CF_UNICODETEXT, text)
    return sequence_number()
