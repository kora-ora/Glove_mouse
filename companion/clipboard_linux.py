"""อ่าน/เขียนข้อความ clipboard บน Linux (X11) — ต้องมี desktop และติดตั้งก่อน: sudo apt install xclip

Windows มี GetClipboardSequenceNumber() ให้เช็คว่าเปลี่ยนโดยไม่ต้องอ่านเนื้อหา แต่ X11 ไม่มีเลขแบบนี้
จึงจำลองด้วยการอ่านเนื้อหาจริงมาแฮชเทียบทุกครั้งที่ sequence_number() ถูกเรียก แล้วเพิ่มตัวนับเมื่อค่าต่าง
"""
import hashlib
import subprocess

_last_hash = None
_seq = 0


def _xclip(args, input_text=None):
    return subprocess.run(
        ["xclip", "-selection", "clipboard", *args],
        input=input_text, capture_output=True, text=True, timeout=2,
    )


def _raw_read():
    try:
        result = _xclip(["-o"])
    except (OSError, subprocess.SubprocessError):
        return None  # ไม่มี xclip ติดตั้ง / ไม่มี X11 display (เช่น รันผ่าน SSH headless)
    return result.stdout if result.returncode == 0 else None


def sequence_number() -> int:
    """poll เนื้อหาจริงมาเทียบแฮชกับครั้งก่อน เปลี่ยน -> เพิ่มตัวนับ (แทนเลข sequence ของ Windows)"""
    global _last_hash, _seq
    text = _raw_read()
    digest = hashlib.md5(text.encode("utf-8")).hexdigest() if text is not None else None
    if digest != _last_hash:
        _last_hash = digest
        _seq += 1
    return _seq


def read_text():
    """คืนข้อความใน clipboard หรือ None ถ้าว่าง/อ่านไม่ได้ (xclip คืนเฉพาะข้อความอยู่แล้ว ไม่ต้องกรองไฟล์/รูปเหมือน Windows)"""
    text = _raw_read()
    if not text or not text.strip():
        return None
    return text


def write_text(text: str) -> int:
    """ใส่ข้อความเข้า clipboard คืนหมายเลข sequence หลังเขียน (ให้ app.py กันส่งกลับเป็นลูป)"""
    _xclip([], input_text=text)
    return sequence_number()
