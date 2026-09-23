"""อ่าน/เขียน clipboard บน Windows"""
import ctypes
import time

import win32clipboard as wc
import win32con

_user32 = ctypes.windll.user32
_user32.GetClipboardSequenceNumber.restype = ctypes.c_uint32

_FMT_EXCLUDE_MONITOR = wc.RegisterClipboardFormat("ExcludeClipboardContentFromMonitorProcessing")
_FMT_CAN_HISTORY = wc.RegisterClipboardFormat("CanIncludeInClipboardHistory")
_FMT_CAN_UPLOAD = wc.RegisterClipboardFormat("CanUploadToCloudClipboard")

_NON_TEXT_FORMATS = (win32con.CF_HDROP, win32con.CF_BITMAP, win32con.CF_DIB, win32con.CF_DIBV5)


def sequence_number() -> int:
    return _user32.GetClipboardSequenceNumber()


class _Clipboard:
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
            if isinstance(raw, (bytes, bytearray)) and len(raw) >= 4 and int.from_bytes(raw[:4], "little") == 0:
                return True
    return False


def read_text():
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
    with _Clipboard():
        wc.EmptyClipboard()
        wc.SetClipboardData(win32con.CF_UNICODETEXT, text)
    return sequence_number()
