"""ทดสอบ Clipboard Service: ส่งข้อความเข้า ESP32 แล้ว GET กลับมาเทียบ

ใช้: pip install bleak
     python clip_roundtrip.py [ชื่อหรือ address ของอุปกรณ์] [ข้อความ]
ต้อง pair ถุงมือกับเครื่องนี้แล้ว (characteristic ต้องเข้ารหัส)
"""
import asyncio
import struct
import sys
import zlib

from bleak import BleakClient, BleakScanner

RX = "7d3c0002-9a4e-4f6b-8c21-5b6e1f0a9d10"
TX = "7d3c0003-9a4e-4f6b-8c21-5b6e1f0a9d10"
START, DATA, END, ACK, NACK, GET, CLEAR = 0x01, 0x02, 0x03, 0x10, 0x11, 0x20, 0x21


def pkt(t, msg_id, seq=0, payload=b""):
    return struct.pack("<BBH", t, msg_id, seq) + payload


async def main(name, text):
    device = await BleakScanner.find_device_by_name(name, timeout=10)
    if device is None:
        sys.exit(f"ไม่พบอุปกรณ์ {name!r}")
    data = text.encode("utf-8")
    inbox = asyncio.Queue()

    async with BleakClient(device) as client:
        await client.start_notify(TX, lambda _, d: inbox.put_nowait(bytes(d)))
        chunk = max(16, min(240, client.mtu_size - 3 - 4))

        # ฝากข้อความ
        await client.write_gatt_char(RX, pkt(START, 1, 0, struct.pack("<HI", len(data), zlib.crc32(data))), response=True)
        for seq, off in enumerate(range(0, len(data), chunk)):
            await client.write_gatt_char(RX, pkt(DATA, 1, seq, data[off:off + chunk]), response=True)
        await client.write_gatt_char(RX, pkt(END, 1), response=True)
        reply = await asyncio.wait_for(inbox.get(), 5)
        print("END ->", "ACK" if reply[0] == ACK else f"NACK err={reply[4]}")
        if reply[0] != ACK:
            return

        # ขอกลับ
        await client.write_gatt_char(RX, pkt(GET, 2), response=True)
        got, total, crc = b"", None, None
        while True:
            p = await asyncio.wait_for(inbox.get(), 5)
            if p[0] == NACK:
                sys.exit(f"GET NACK err={p[4]}")
            if p[0] == START:
                total, crc = struct.unpack("<HI", p[4:10])
            elif p[0] == DATA:
                got += p[4:]
            elif p[0] == END:
                break

        ok = got == data and total == len(data) and crc == zlib.crc32(data)
        print(f"GET -> {len(got)} ไบต์, {'ตรงเป๊ะ' if ok else 'ไม่ตรง!'}")


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1] if len(sys.argv) > 1 else "Glove Air Mouse",
                     sys.argv[2] if len(sys.argv) > 2 else "สวัสดี clipboard " * 40))
