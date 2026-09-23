"""Glove Clipboard: Sync clipboard ระหว่างเครื่องผ่านถุงมือ Air Mouse"""
import argparse
import asyncio
import logging
import queue
import sys
import threading
import tkinter as tk
from tkinter import scrolledtext

import pystray
from PIL import Image, ImageDraw

if sys.platform == "win32":
    import clipboard_win as clip
else:
    import clipboard_linux as clip
from ble_client import GloveLink
from thingspeak_worker import ThingSpeakWorker

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


class QueueLogHandler(logging.Handler):
    """ส่ง log record เข้า queue แทนการเขียน Text widget ตรงๆ (log มาจากเธรด asyncio/BLE
    แต่ Tkinter widget แก้ได้จาก main thread เท่านั้น) ฝั่งหน้าต่างดึงไปแสดงเองผ่าน root.after()"""

    def __init__(self):
        super().__init__()
        self.queue = queue.Queue()

    def emit(self, record):
        self.queue.put(self.format(record))


class App:
    def __init__(self, address, name, thingspeak_key=None):
        self.state = "disconnected"
        self.paused = False
        self.stopping = False
        self._ignore_seq = None  # หมายเลข clipboard sequence ที่แอปเป็นคนเขียนเอง (ไม่ใช่การ copy ใหม่)
        self._reconnect_count = 0  # จำนวนครั้งที่ลิงก์ BLE ต่อใหม่ (นับตั้งแต่เปิดแอป) -> field4
        self.ts_worker = ThingSpeakWorker(thingspeak_key)  # ไม่ใส่ key = เธรดนี้ไม่ทำอะไรเลย (ดู thingspeak_worker.py)
        self.ts_worker.start()
        self.loop = asyncio.new_event_loop()
        self.link = GloveLink(self._on_remote_text, self._on_state, address=address, name=name,
                              on_relink=self._on_relink)
        self.icon = pystray.Icon(
            "glove-clipboard",
            make_image(STATE_COLOR[self.state]),
            "Glove Clipboard",
            menu=pystray.Menu(
                pystray.MenuItem(lambda _item: self._status_text(), None, enabled=False),
                pystray.MenuItem("เปิดหน้าต่าง", self._show_window, default=True),
                pystray.MenuItem("หยุด sync ชั่วคราว", self._toggle_pause, checked=lambda _item: self.paused),
                pystray.MenuItem("ออก", self._quit),
            ),
        )

        self._log_handler = QueueLogHandler()
        self._log_handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s", "%H:%M:%S"))
        logging.getLogger("glove").addHandler(self._log_handler)
        self._build_window()

    # ---------- หน้าต่างหลัก (tray ยังทำงานคู่กันตามเดิม ปิดหน้าต่างแค่ซ่อนไม่เลิกโปรแกรม) ----------
    def _build_window(self):
        self.root = tk.Tk()
        self.root.title("Glove Clipboard")
        self.root.geometry("420x320")
        self.root.protocol("WM_DELETE_WINDOW", self._hide_window)

        self.status_var = tk.StringVar(value=self._status_text())
        tk.Label(self.root, textvariable=self.status_var, font=("", 11, "bold")).pack(anchor="w", padx=10, pady=(10, 4))

        self.pause_var = tk.BooleanVar(value=self.paused)
        tk.Checkbutton(self.root, text="หยุด sync ชั่วคราว", variable=self.pause_var,
                       command=self._toggle_pause_from_window).pack(anchor="w", padx=10)

        self.log_text = scrolledtext.ScrolledText(self.root, height=14, state="disabled", wrap="word")
        self.log_text.pack(fill="both", expand=True, padx=10, pady=10)

        tk.Button(self.root, text="ออก", command=self._quit).pack(anchor="e", padx=10, pady=(0, 10))

        self.root.after(200, self._drain_log)

    def _drain_log(self):
        while True:
            try:
                line = self._log_handler.queue.get_nowait()
            except queue.Empty:
                break
            self.log_text.configure(state="normal")
            self.log_text.insert("end", line + "\n")
            self.log_text.see("end")
            self.log_text.configure(state="disabled")
        self.root.after(200, self._drain_log)

    def _show_window(self, *_args):
        self.root.after(0, lambda: (self.root.deiconify(), self.root.lift()))

    def _hide_window(self):
        self.root.withdraw()

    def _toggle_pause_from_window(self):
        self._toggle_pause(None, None)

    # ---------- UI ----------
    def _status_text(self):
        text = STATE_TEXT[self.state]
        return text + " (หยุดชั่วคราว)" if self.paused else text

    def _refresh_icon(self):
        color = PAUSED_COLOR if self.paused else STATE_COLOR[self.state]
        self.icon.icon = make_image(color)
        self.icon.title = "Glove Clipboard: " + self._status_text()
        self.icon.update_menu()
        if hasattr(self, "root"):
            self.root.after(0, self._refresh_window)

    def _refresh_window(self):
        self.status_var.set(self._status_text())
        self.pause_var.set(self.paused)

    def _notify(self, message):
        try:
            self.icon.notify(message, "Glove Clipboard")
        except Exception:
            log.info(message)

    def _toggle_pause(self, _icon, _item):
        self.paused = not self.paused
        self._refresh_icon()

    def _quit(self, *_args):
        self.stopping = True
        self.loop.call_soon_threadsafe(self.link.stop)
        self.icon.stop()
        self.root.after(0, self.root.destroy)

    # ---------- callbacks จาก GloveLink (รันใน asyncio thread) ----------
    def _on_state(self, state):
        self.state = state
        self._refresh_icon()

    def _on_relink(self, _lived_seconds):
        # เรียกทุกครั้งที่ลิงก์ BLE ต่อใหม่ (รวมครั้งแรกตอนเปิดแอป) -> นับสะสมไว้ดูความเสถียรระยะยาวเป็นกราฟ
        self._reconnect_count += 1
        self.ts_worker.submit(field4=self._reconnect_count)

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
        self.ts_worker.submit(field1=len(text))
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
            ok, message, latency_ms = await self.link.send_text(text)
            if ok:
                log.info("ส่งข้อความ %d ตัวอักษรแล้ว (%.0f ms)", len(text), latency_ms)
                self.ts_worker.submit(field1=len(text), field5=round(latency_ms, 1))
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
        threading.Thread(target=self.icon.run, daemon=True).start()  # tray ทำงานคู่ขนานไปกับหน้าต่าง
        self.root.mainloop()  # หน้าต่างหลักครองเมนไทรด์ (Tkinter ต้องรันบน main thread)


def main():
    if sys.platform not in ("win32", "linux"):
        sys.exit("Glove Clipboard รองรับเฉพาะ Windows และ Linux")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--address", help="BLE address ของถุงมือ (ถ้าไม่ใส่ จะหาจากรายการที่ pair ใน Windows เอง, Linux ต้องระบุเอง)")
    parser.add_argument("--name", default="Glove Air Mouse", help="ชื่ออุปกรณ์ (ใช้ตอนสแกน)")
    parser.add_argument("--thingspeak-key", default="KFEW4NNX2XD13JI1",
                        help="Write API Key ของ ThingSpeak channel (ค่าเริ่มต้นคือ key ของโปรเจกต์นี้)")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    App(args.address, args.name, args.thingspeak_key).run()


if __name__ == "__main__":
    main()
