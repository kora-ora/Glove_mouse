"""BLE client สำหรับเชื่อมต่อ clipboard service ของถุงมือ"""
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
RECONNECT_MAX_DELAY = 30.0
STABLE_SECONDS = 20.0


class GloveLink:
    def __init__(self, on_text, on_state, address=None, name="Glove Air Mouse", on_relink=None):
        self.address = address
        self.name = name
        self._on_text = on_text
        self._on_state = on_state
        self._on_relink = on_relink
        self._client = None
        self._connected = asyncio.Event()
        self._lost = asyncio.Event()
        self._inbox = asyncio.Queue()
        self._lock = asyncio.Lock()
        self._msg_id = 0
        self._last_crc = None
        self._last_sent_at = 0.0
        self._tasks = set()
        self.stopping = False

    @property
    def connected(self) -> bool:
        return self._connected.is_set()

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

            wait = delay + random.uniform(0.0, 1.0)
            log.info("ต่อได้นาน %.1f วินาที จะลองใหม่ใน %.1f วินาที", lived, wait)
            if self._on_relink:
                self._on_relink(lived)
            await asyncio.sleep(wait)
            delay = RECONNECT_MIN_DELAY if lived >= STABLE_SECONDS else min(delay * 2, RECONNECT_MAX_DELAY)

    def _known_device(self, address):
        return BLEDevice(address, self.name, None)

    async def _resolve_target(self):
        if self.address:
            return self._known_device(self.address)
        paired = await asyncio.to_thread(find_paired_address, self.name)
        if paired:
            log.info("พบถุงมือที่ pair ไว้: %s", paired)
            return self._known_device(paired)
        device = await BleakScanner.find_device_by_name(self.name, timeout=10)
        if device is not None:
            return device
        raise RuntimeError(f"ไม่พบถุงมือ {self.name!r} (pair ก่อน หรือระบุ --address)")

    async def _session(self):
        target = await self._resolve_target()

        self._lost.clear()
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
        return time.monotonic() - connected_at

    def stop(self):
        self.stopping = True
        self._lost.set()

    def _on_tx(self, _sender, data: bytearray):
        self._inbox.put_nowait(bytes(data))

    def _on_status(self, _sender, data: bytearray):
        try:
            state, length = p.parse_status(bytes(data))
        except p.ProtocolError:
            return
        # ถ้าเครื่องนี้เพิ่งส่งข้อความเอง ไม่ต้องแย่งดึงข้อมูลตัวเองกลับ
        if time.monotonic() - self._last_sent_at < 2.0:
            return
        if state == p.STATE_HAS and length > 0:
            task = asyncio.ensure_future(self._fetch())
            self._tasks.add(task)
            task.add_done_callback(self._tasks.discard)

    def _next_id(self) -> int:
        self._msg_id = self._msg_id % 255 + 1
        return self._msg_id

    def _drain(self):
        while not self._inbox.empty():
            self._inbox.get_nowait()

    async def _write(self, packet: bytes):
        await self._client.write_gatt_char(p.RX_UUID, packet, response=True)

    async def send_text(self, text: str):
        data = text.encode("utf-8")
        if len(data) > p.MAX_BYTES:
            return False, f"ข้อความยาว {len(data)} ไบต์ เกิน {p.MAX_BYTES}", 0.0
        if not self.connected:
            return False, "ยังไม่ได้ต่อถุงมือ", 0.0

        started = time.monotonic()
        async with self._lock:
            self._drain()
            self._last_crc = p.crc32(data)
            self._last_sent_at = time.monotonic()
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

    async def _fetch(self):
        async with self._lock:
            if not self.connected:
                return
            self._drain()
            data = None
            for attempt in range(3):
                reassembler = p.Reassembler()
                try:
                    await self._write(p.pack(p.GET, self._next_id()))
                    while data is None:
                        packet = await asyncio.wait_for(self._inbox.get(), ACK_TIMEOUT)
                        data = reassembler.feed(packet)
                    break
                except (asyncio.TimeoutError, p.ProtocolError) as exc:
                    if attempt < 2:
                        await asyncio.sleep(0.15 * (attempt + 1))
                        self._drain()
                        continue
                    log.info("ดึงข้อความไม่สำเร็จ: %s", exc)
                    return
                except Exception as exc:
                    log.warning("ดึงข้อความผิดพลาด: %s", exc)
                    return

            if data is None:
                return
            crc = p.crc32(data)
            if crc == self._last_crc:
                return
            try:
                text = data.decode("utf-8")
            except UnicodeDecodeError:
                return
            self._last_crc = crc

        self._on_text(text)
