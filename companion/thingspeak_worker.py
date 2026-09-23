"""เธรดสำหรับส่ง event ขึ้น ThingSpeak แยกเพื่อไม่ให้บล็อกงานหลัก"""
import logging
import queue
import threading

import thingspeak

log = logging.getLogger("glove")
QUEUE_MAXSIZE = 50


class ThingSpeakWorker(threading.Thread):
    def __init__(self, api_key):
        super().__init__(name="ThingSpeakWorker", daemon=True)
        self.api_key = api_key
        self._queue = queue.Queue(maxsize=QUEUE_MAXSIZE)

    def enabled(self) -> bool:
        return bool(self.api_key)

    def submit(self, **fields):
        if not self.enabled():
            return
        try:
            self._queue.put_nowait(fields)
        except queue.Full:
            log.warning("คิว ThingSpeak เต็ม")

    def run(self):
        while True:
            fields = self._queue.get()
            if thingspeak.log_event(self.api_key, **fields):
                log.info("ส่งขึ้น ThingSpeak: %s", fields)
