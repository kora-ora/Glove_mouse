"""Glove Clipboard: โปรแกรม tray บน Windows ที่ sync ข้อความ clipboard ผ่านถุงมือ

  python app.py [--address AA:BB:CC:DD:EE:FF] [--name "Glove Air Mouse"]

Ctrl+C บนเครื่องนี้ -> ส่งเข้า ESP32 -> อีกเครื่องที่รันโปรแกรมนี้ได้รับและใส่เข้า clipboard
"""
import argparse
import asyncio
import logging
import sys
import threading

import pystray
from PIL import Image, ImageDraw

import clipboard_win as clip
from ble_client import GloveLink

log = logging.getLogger("glove")

POLL_INTERVAL = 0.25  # วินาที

STATE_TEXT = {
    "connected": "เชื่อมต่อถุงมือแล้ว",
    "connecting": "กำลังเชื่อมต่อ...",
    "disconnected": "ยังไม่ได้ต่อถุงมือ",
}
STATE_COLOR = {
    "connected": (46, 160, 67),
    "connecting": (210, 153, 34),
    "disconnected": (128, 128, 128),
}
PAUSED_COLOR = (200, 60, 60)


def make_image(color):
    image = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    ImageDraw.Draw(image).ellipse((6, 6, 58, 58), fill=color + (255,))
    return image


class App:
    def __init__(self, address, name):
        self.state = "disconnected"
        self.paused = False
        self.stopping = False
        self._ignore_seq = None  # หมายเลข clipboard sequence ที่แอปเป็นคนเขียนเอง (ไม่ใช่การ copy ใหม่)
        self.loop = asyncio.new_event_loop()
        self.link = GloveLink(self._on_remote_text, self._on_state, address=address, name=name)
        self.icon = pystray.Icon(
            "glove-clipboard",
            make_image(STATE_COLOR[self.state]),
            "Glove Clipboard",
            menu=pystray.Menu(
                pystray.MenuItem(lambda _item: self._status_text(), None, enabled=False),
                pystray.MenuItem("หยุด sync ชั่วคราว", self._toggle_pause, checked=lambda _item: self.paused),
                pystray.MenuItem("ออก", self._quit),
            ),
        )

    # ---------- UI ----------
    def _status_text(self):
        text = STATE_TEXT[self.state]
        return text + " (หยุดชั่วคราว)" if self.paused else text

    def _refresh_icon(self):
        color = PAUSED_COLOR if self.paused else STATE_COLOR[self.state]
        self.icon.icon = make_image(color)
        self.icon.title = "Glove Clipboard: " + self._status_text()
        self.icon.update_menu()

    def _notify(self, message):
        try:
            self.icon.notify(message, "Glove Clipboard")
        except Exception:
            log.info(message)

    def _toggle_pause(self, _icon, _item):
        self.paused = not self.paused
        self._refresh_icon()

    def _quit(self, _icon, _item):
        self.stopping = True
        self.loop.call_soon_threadsafe(self.link.stop)
        self.icon.stop()

    # ---------- callbacks จาก GloveLink (รันใน asyncio thread) ----------
    def _on_state(self, state):
        self.state = state
        self._refresh_icon()

    def _on_remote_text(self, text):
        if self.paused:
            return
        try:
            # จำหมายเลข sequence หลังเขียน: การเปลี่ยนแปลงนี้มาจากเราเอง ห้ามส่งกลับไปเป็น "การ copy ใหม่"
            self._ignore_seq = clip.write_text(text)
        except OSError as exc:
            log.warning("ใส่ clipboard ไม่ได้: %s", exc)
            return
        log.info("ได้รับข้อความ %d ตัวอักษร", len(text))
        self._notify("ได้รับข้อความจากอีกเครื่องแล้ว กด Ctrl+V ได้เลย")

    # ---------- ดัก clipboard ----------
    async def _watch_clipboard(self):
        last = clip.sequence_number()
        while not self.stopping:
            await asyncio.sleep(POLL_INTERVAL)
            seq = clip.sequence_number()
            if seq == last:
                continue
            last = seq
            if seq == self._ignore_seq or self.paused or not self.link.connected:
                continue
            text = await asyncio.get_running_loop().run_in_executor(None, clip.read_text)
            if text is None:
                continue  # ไม่ใช่ข้อความล้วน / password manager ห้ามยุ่ง / ว่าง
            ok, message = await self.link.send_text(text)
            if ok:
                log.info("ส่งข้อความ %d ตัวอักษรแล้ว", len(text))
            else:
                log.warning("ส่งไม่สำเร็จ: %s", message)
                self._notify(message)

    async def _main(self):
        await asyncio.gather(self.link.run(), self._watch_clipboard())

    def _run_loop(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_until_complete(self._main())

    def run(self):
        threading.Thread(target=self._run_loop, daemon=True).start()
        self.icon.run()  # บล็อกที่ main thread จนกว่าจะเลือก "ออก"


def main():
    if sys.platform != "win32":
        sys.exit("Glove Clipboard รองรับเฉพาะ Windows")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--address", help="BLE address ของถุงมือ (ถ้าไม่ใส่ จะหาจากรายการที่ pair ใน Windows เอง)")
    parser.add_argument("--name", default="Glove Air Mouse", help="ชื่ออุปกรณ์ (ใช้ตอนสแกน)")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    App(args.address, args.name).run()


if __name__ == "__main__":
    main()
