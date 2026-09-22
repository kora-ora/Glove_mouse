"""ต่อ BLE เข้า clipboard service ของถุงมือ: ส่งข้อความเข้า ESP32 และรับข้อความที่เครื่องอื่นฝากไว้"""
import asyncio
import logging
import random
import time

from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice

import protocol as p
from discovery import find_paired_address

log = logging.getLogger("glove")

ACK_TIMEOUT = 5.0
RECONNECT_MIN_DELAY = 2.0
RECONNECT_MAX_DELAY = 30.0  # ต่อไม่ได้ให้ถอยห่างขึ้นเรื่อยๆ การพยายามถี่ๆ จะรบกวนลิงก์ของเครื่องอื่นที่ต่ออยู่
STABLE_SECONDS = 20.0       # ลิงก์อยู่ได้นานเท่านี้ถึงถือว่าเสถียร (รีเซ็ตเวลารอกลับเป็นค่าต่ำสุด)


class GloveLink:
    """
    on_text(text): เรียกเมื่อได้ข้อความใหม่จากเครื่องอื่น (ผ่าน ESP32)
    on_state(state): "disconnected" | "connecting" | "connected"
    """

    def __init__(self, on_text, on_state, address=None, name="Glove Air Mouse", on_relink=None):
        self.address = address                  # ระบุเอง (ชนะทุกอย่าง)
        self.name = name
        self._on_text = on_text
        self._on_state = on_state
        self._on_relink = on_relink  # (lived_seconds) เรียกทุกครั้งที่ต่อใหม่ ไว้ดูความเสถียรของ BLE ย้อนหลัง
        self._client = None
        self._connected = asyncio.Event()
        self._lost = asyncio.Event()
        self._inbox = asyncio.Queue()
        self._lock = asyncio.Lock()   # ท่อ TX ใช้ได้ทีละงาน (ส่ง หรือ GET)
        self._msg_id = 0
        self._last_crc = None         # CRC ของข้อความล่าสุดที่ส่ง/รับ ใช้กันส่งกลับมาเองเป็นลูป
        self._tasks = set()
        self.stopping = False

    @property
    def connected(self) -> bool:
        return self._connected.is_set()

    # ---------- การเชื่อมต่อ ----------
    async def run(self):
        delay = RECONNECT_MIN_DELAY
        while not self.stopping:
            lived = 0.0
            try:
                self._on_state("connecting")
                lived = await self._session()
            except Exception as exc:
                log.warning("เชื่อมต่อไม่สำเร็จ: %s", exc)
            self._connected.clear()
            self._on_state("disconnected")
            if self.stopping:
                break

            # ลิงก์อยู่ได้ไม่นานถือว่าไม่เสถียร: ต้องถอยห่างขึ้นเรื่อยๆ ไม่ใช่รีเซ็ตกลับไปรอสั้น (กันลูปต่อ-หลุดถี่ๆ)
            # สุ่มเวลาเพิ่มเล็กน้อย กัน app ของสองเครื่องต่อพร้อมกันเป็นจังหวะเดียวกันจนแย่งกัน
            wait = delay + random.uniform(0.0, 1.0)
            log.info("ต่อได้นาน %.1f วินาที จะลองใหม่ใน %.1f วินาที", lived, wait)
            if self._on_relink:
                self._on_relink(lived)
            await asyncio.sleep(wait)
            delay = RECONNECT_MIN_DELAY if lived >= STABLE_SECONDS else min(delay * 2, RECONNECT_MAX_DELAY)

    def _known_device(self, address):
        # ส่ง BLEDevice แทนสตริง address: bleak จะข้ามการสแกนแล้วต่อตรงไปที่ address นั้น
        # (ถุงมือที่ต่อ Windows อยู่แล้วไม่ advertise การสแกนจึงไม่เจอ แต่ต่อ GATT ตรงๆ ได้)
        return BLEDevice(address, self.name, None)

    async def _resolve_target(self):
        """ลำดับการหาอุปกรณ์: --address → รายการที่ pair ใน Windows → สแกนหาชื่อ (ไม่เจอ = ยังไม่ต่อ ไม่เดา address)"""
        if self.address:
            return self._known_device(self.address)
        paired = await asyncio.to_thread(find_paired_address, self.name)
        if paired:
            log.info("พบถุงมือที่ pair ไว้ใน Windows: %s", paired)
            return self._known_device(paired)
        device = await BleakScanner.find_device_by_name(self.name, timeout=10)
        if device is not None:
            return device
        raise RuntimeError(f"ไม่พบถุงมือ {self.name!r} ที่ pair ไว้ใน Windows (pair ก่อน หรือระบุ --address)")

    async def _session(self):
        target = await self._resolve_target()

        self._lost.clear()
        # use_cached_services=False: Windows เก็บตาราง GATT handle ของ address นี้ไว้ข้ามการต่อ
        # ถ้าฝั่งบอร์ด reflash แล้ว service/characteristic ขยับ handle เดิม cache จะไม่ตรงของจริง
        # ทำให้ start_notify พังแบบ "Unreachable" หรือต่อไม่ติดเลย ต้องบังคับ discover ใหม่ทุกครั้ง
        async with BleakClient(target, disconnected_callback=lambda _c: self._lost.set(),
                               winrt=dict(use_cached_services=False)) as client:
            self._client = client
            await client.start_notify(p.TX_UUID, self._on_tx)
            await client.start_notify(p.STATUS_UUID, self._on_status)
            self._connected.set()
            self._on_state("connected")
            connected_at = time.monotonic()
            log.info("ต่อถุงมือแล้ว (MTU %s)", client.mtu_size)
            await self._lost.wait()
        self._client = None
        return time.monotonic() - connected_at  # ใช้ตัดสินว่าลิงก์เสถียรพอจะรีเซ็ตเวลารอไหม

    def stop(self):
        self.stopping = True
        self._lost.set()

    # ---------- notify จาก ESP32 ----------
    def _on_tx(self, _sender, data: bytearray):
        self._inbox.put_nowait(bytes(data))

    def _on_status(self, _sender, data: bytearray):
        try:
            state, length = p.parse_status(bytes(data))
        except p.ProtocolError:
            return
        # มีข้อความใหม่บนถุงมือ -> ไปดึงมา (ข้อความที่เรา/เครื่องเราเป็นคนส่งจะถูกข้ามด้วย CRC)
        if state == p.STATE_HAS and length > 0:
            task = asyncio.ensure_future(self._fetch())
            self._tasks.add(task)
            task.add_done_callback(self._tasks.discard)

    # ---------- ส่งข้อความเข้า ESP32 ----------
    def _next_id(self) -> int:
        self._msg_id = self._msg_id % 255 + 1
        return self._msg_id

    def _drain(self):
        while not self._inbox.empty():
            self._inbox.get_nowait()

    async def _write(self, packet: bytes):
        await self._client.write_gatt_char(p.RX_UUID, packet, response=True)

    async def send_text(self, text: str):
        """คืน (ok, ข้อความอธิบาย, ms ที่ใช้ตั้งแต่เริ่มส่งจนถุงมือ ACK — ไม่ใช่เวลาที่อีกเครื่องได้รับจริง)"""
        data = text.encode("utf-8")
        if len(data) > p.MAX_BYTES:
            return False, f"ข้อความยาว {len(data)} ไบต์ เกิน {p.MAX_BYTES}", 0.0
        if not self.connected:
            return False, "ยังไม่ได้ต่อถุงมือ", 0.0

        started = time.monotonic()
        async with self._lock:
            self._drain()
            self._last_crc = p.crc32(data)  # ตั้งก่อนส่ง: STATUS notify ที่ตามมาจะได้ไม่ดึงข้อความตัวเองกลับ
            try:
                for packet in p.encode_message(self._next_id(), data, self._client.mtu_size):
                    await self._write(packet)
                reply = await asyncio.wait_for(self._inbox.get(), ACK_TIMEOUT)
            except asyncio.TimeoutError:
                return False, "ถุงมือไม่ตอบรับ (timeout)", 0.0
            except Exception as exc:
                return False, f"ส่งไม่สำเร็จ: {exc}", 0.0
        elapsed_ms = (time.monotonic() - started) * 1000

        ptype, _mid, _seq, payload = p.parse(reply)
        if ptype == p.ACK:
            return True, "ส่งแล้ว", elapsed_ms
        code = payload[0] if payload else 0
        return False, p.ERROR_NAMES.get(code, f"ถุงมือปฏิเสธ (code {code})"), 0.0

    # ---------- ดึงข้อความจาก ESP32 ----------
    async def _fetch(self):
        async with self._lock:
            if not self.connected:
                return
            self._drain()
            reassembler = p.Reassembler()
            try:
                await self._write(p.pack(p.GET, self._next_id()))
                data = None
                while data is None:
                    packet = await asyncio.wait_for(self._inbox.get(), ACK_TIMEOUT)
                    data = reassembler.feed(packet)
            except (asyncio.TimeoutError, p.ProtocolError) as exc:
                log.info("ดึงข้อความไม่สำเร็จ: %s", exc)
                return
            except Exception as exc:
                log.warning("ดึงข้อความผิดพลาด: %s", exc)
                return

            crc = p.crc32(data)
            if crc == self._last_crc:
                return  # เป็นข้อความเดียวกับที่เพิ่งส่ง/รับ
            try:
                text = data.decode("utf-8")
            except UnicodeDecodeError:
                log.info("ข้อความไม่ใช่ UTF-8 ข้าม")
                return
            self._last_crc = crc

        self._on_text(text)
