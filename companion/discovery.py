"""หา BLE address ของถุงมือจากรายการอุปกรณ์ที่ pair ไว้ใน Windows หรือ Linux"""
import re
import subprocess
import sys

_INSTANCE_RE = re.compile(r"BTHLE\\DEV_([0-9A-Fa-f]{12})", re.IGNORECASE)


def parse_address(output: str):
    found = []
    for line in output.splitlines():
        status, _, instance_id = line.partition("|")
        match = _INSTANCE_RE.search(instance_id)
        if match:
            mac = match.group(1).upper()
            found.append((status.strip() != "OK", ":".join(mac[i:i + 2] for i in range(0, 12, 2))))
    if not found:
        return None
    found.sort(key=lambda item: item[0])
    return found[0][1]


def find_paired_address_windows(name: str, timeout: float = 15.0):
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


def find_paired_address_linux(name: str, timeout: float = 5.0):
    """หา MAC address จาก BlueZ ผ่าน bluetoothctl devices"""
    try:
        result = subprocess.run(
            ["bluetoothctl", "devices"],
            capture_output=True, text=True, timeout=timeout,
        )
        for line in result.stdout.splitlines():
            # รูปแบบ: "Device 20:9B:A9:67:CE:92 Glove Air Mouse"
            parts = line.strip().split(maxsplit=2)
            if len(parts) >= 3 and parts[0] == "Device":
                mac, dev_name = parts[1], parts[2]
                if name.lower() in dev_name.lower():
                    return mac.upper()
    except (OSError, subprocess.SubprocessError):
        pass
    return None


def find_paired_address(name: str, timeout: float = 15.0):
    if sys.platform == "win32":
        return find_paired_address_windows(name, timeout)
    elif sys.platform.startswith("linux"):
        return find_paired_address_linux(name, timeout)
    return None
