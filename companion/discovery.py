"""หา BLE address ของถุงมือจากรายการอุปกรณ์ที่ pair ไว้ใน Windows

ตอนถุงมือต่อ Windows อยู่ มันไม่ advertise การสแกนหาชื่อจึงไม่เจอ แต่ Windows รู้ address อยู่แล้ว
(InstanceId ของอุปกรณ์ BLE ที่ pair ไว้ = BTHLE\\DEV_<MAC 12 หลัก hex>)
"""
import re
import subprocess

_INSTANCE_RE = re.compile(r"BTHLE\\DEV_([0-9A-Fa-f]{12})", re.IGNORECASE)


def parse_address(output: str):
    """รับผลลัพธ์บรรทัดละ 'Status|InstanceId' คืน address รูป AA:BB:.. (เลือกตัวที่ Status=OK ก่อน) หรือ None"""
    found = []
    for line in output.splitlines():
        status, _, instance_id = line.partition("|")
        match = _INSTANCE_RE.search(instance_id)
        if match:
            mac = match.group(1).upper()
            found.append((status.strip() != "OK", ":".join(mac[i:i + 2] for i in range(0, 12, 2))))
    if not found:
        return None
    found.sort(key=lambda item: item[0])  # OK ขึ้นก่อน
    return found[0][1]


def find_paired_address(name: str, timeout: float = 15.0):
    """คืน address ของอุปกรณ์ BLE ชื่อ name ที่ pair ไว้ใน Windows หรือ None ถ้าไม่เจอ/ถามไม่ได้"""
    quoted = name.replace("'", "''")
    script = (
        f"Get-PnpDevice -FriendlyName '{quoted}' -ErrorAction SilentlyContinue | "
        "ForEach-Object { $_.Status + '|' + $_.InstanceId }"
    )
    try:
        result = subprocess.run(
            ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
            capture_output=True, text=True, timeout=timeout,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
    except (OSError, subprocess.SubprocessError):
        return None
    return parse_address(result.stdout)
