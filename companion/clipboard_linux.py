"""อ่าน/เขียน clipboard บน Linux (X11 xclip)"""
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
        return None
    return result.stdout if result.returncode == 0 else None


def sequence_number() -> int:
    global _last_hash, _seq
    text = _raw_read()
    digest = hashlib.md5(text.encode("utf-8")).hexdigest() if text is not None else None
    if digest != _last_hash:
        _last_hash = digest
        _seq += 1
    return _seq


def read_text():
    text = _raw_read()
    if not text or not text.strip():
        return None
    return text


def write_text(text: str) -> int:
    _xclip([], input_text=text)
    return sequence_number()
