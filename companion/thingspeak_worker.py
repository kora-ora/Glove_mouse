"""เธรดแยกสำหรับส่ง event ขึ้น ThingSpeak โดยเฉพาะ (producer/consumer ผ่าน queue.Queue)

ทำไมต้องแยกเธรด: thingspeak.log_event() เป็น blocking HTTP call (urllib) ถ้าเรียกตรงจากเธรดที่เฝ้า
clipboard/BLE อยู่ เน็ตช้าหรือ ThingSpeak ตอบช้าจะไปหน่วงงานหลักด้วย จึงแยกเธรดเดียวรับผิดชอบเรื่องนี้
อย่างเดียว: เธรดอื่น submit() ใส่ queue (ไม่ block เลย) แล้วเธรดนี้ค่อยๆ ดึงไปยิงทีละอันในเวลาของตัวเอง
Queue เป็นจุดแชร์ข้อมูลระหว่างเธรดที่ thread-safe อยู่แล้วในตัว (ไม่ต้องมี Lock เอง)
"""
import logging
import queue
import threading

import thingspeak

log = logging.getLogger("glove")

QUEUE_MAXSIZE = 50  # กันคิวบวมไม่จำกัดถ้าออฟไลน์/คีย์ผิดนานๆ


class ThingSpeakWorker(threading.Thread):
    def __init__(self, api_key):
        super().__init__(name="ThingSpeakWorker", daemon=True)
        self.api_key = api_key
        self._queue = queue.Queue(maxsize=QUEUE_MAXSIZE)

    def enabled(self) -> bool:
        return bool(self.api_key)

    def submit(self, **fields):
        """เรียกจากเธรด/เธรดอื่นได้ปลอดภัย ไม่ block ผู้เรียกเลย (คืนทันที)"""
        if not self.enabled():
            return
        try:
            self._queue.put_nowait(fields)
        except queue.Full:
            log.warning("คิว ThingSpeak เต็ม ทิ้ง event นี้ไป (เน็ตอาจช้า/ออฟไลน์อยู่)")

    def run(self):
        # ถ้าไม่ได้เปิดใช้ (ไม่มี key) เธรดนี้ก็แค่ค้างรอเฉยๆ ไม่กินทรัพยากรอะไร
        while True:
            fields = self._queue.get()
            if thingspeak.log_event(self.api_key, **fields):
                log.info("ส่งขึ้น ThingSpeak: %s", fields)
