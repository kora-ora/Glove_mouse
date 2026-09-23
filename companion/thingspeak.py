"""ส่ง event ขึ้น ThingSpeak ผ่าน HTTP GET (เรียกจาก ThingSpeakWorker เท่านั้น)"""
import time
import urllib.parse
import urllib.request

MIN_INTERVAL_S = 15.0
_last_sent = 0.0


def log_event(api_key: str, **fields) -> bool:
    global _last_sent
    if not api_key or not fields:
        return False
    now = time.monotonic()
    if now - _last_sent < MIN_INTERVAL_S:
        return False
    _last_sent = now

    params = {"api_key": api_key, **fields}
    url = "https://api.thingspeak.com/update?" + urllib.parse.urlencode(params)
    try:
        urllib.request.urlopen(url, timeout=5).close()
    except Exception:
        pass
    return True
