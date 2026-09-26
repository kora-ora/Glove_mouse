"""โปรโตคอล clipboard ระหว่างเครื่อง <-> ESP32 (ตรงกับ ClipboardService.cpp)

Packet: [type u8][msgId u8][seq u16 LE][payload...]
ไม่มี dependency ภายนอก
"""
import struct
import zlib

SERVICE_UUID = "7d3c0001-9a4e-4f6b-8c21-5b6e1f0a9d10"
RX_UUID = "7d3c0002-9a4e-4f6b-8c21-5b6e1f0a9d10"      # เครื่อง -> ESP32 (write)
TX_UUID = "7d3c0003-9a4e-4f6b-8c21-5b6e1f0a9d10"      # ESP32 -> เครื่อง (notify)
STATUS_UUID = "7d3c0004-9a4e-4f6b-8c21-5b6e1f0a9d10"  # [state u8][len u16 LE]

START, DATA, END = 0x01, 0x02, 0x03
ACK, NACK = 0x10, 0x11
GET, CLEAR = 0x20, 0x21

STATE_EMPTY, STATE_HAS, STATE_RECEIVING = 0, 1, 2

MAX_BYTES = 16384  # ต้องเท่ากับ Config::CLIP_MAX_BYTES ในเฟิร์มแวร์
HEADER_LEN = 4
MIN_CHUNK, MAX_CHUNK = 16, 240

ERROR_NAMES = {
    1: "ข้อความใหญ่เกินไป",
    2: "ลำดับ/ชิ้นข้อมูลไม่ครบ",
    3: "CRC ไม่ตรง",
    4: "หมดเวลา",
    5: "สถานะไม่ถูกต้อง",
    6: "ไม่มีข้อความบนถุงมือ",
}

ERR_TOO_BIG = 1
ERR_BAD_SEQ = 2
ERR_BAD_CRC = 3
ERR_TIMEOUT = 4
ERR_BAD_STATE = 5
ERR_EMPTY = 6


class ProtocolError(Exception):
    pass


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def pack(ptype: int, msg_id: int, seq: int = 0, payload: bytes = b"") -> bytes:
    return struct.pack("<BBH", ptype, msg_id, seq) + payload


def parse(packet: bytes):
    """คืน (type, msg_id, seq, payload)"""
    if len(packet) < HEADER_LEN:
        raise ProtocolError("packet สั้นเกินไป")
    ptype, msg_id, seq = struct.unpack_from("<BBH", packet)
    return ptype, msg_id, seq, packet[HEADER_LEN:]


def chunk_size(mtu: int) -> int:
    """ขนาด payload ต่อชิ้น = MTU - 3 (ATT) - 4 (header) จำกัด 16..240"""
    return max(MIN_CHUNK, min(MAX_CHUNK, mtu - 3 - HEADER_LEN))


def encode_message(msg_id: int, data: bytes, mtu: int = 23):
    """แตกข้อความเป็น packet START, DATA..., END"""
    if not data or len(data) > MAX_BYTES:
        raise ProtocolError(f"ข้อความต้องยาว 1..{MAX_BYTES} ไบต์")
    step = chunk_size(mtu)
    packets = [pack(START, msg_id, 0, struct.pack("<HI", len(data), crc32(data)))]
    for seq, offset in enumerate(range(0, len(data), step)):
        packets.append(pack(DATA, msg_id, seq, data[offset:offset + step]))
    packets.append(pack(END, msg_id))
    return packets


def parse_status(value: bytes):
    """คืน (state, length) จาก CLIP_STATUS"""
    if len(value) < 3:
        raise ProtocolError("status สั้นเกินไป")
    state, length = struct.unpack_from("<BH", value)
    return state, length


class Reassembler:
    """ประกอบข้อความที่ ESP32 ส่งกลับ (START, DATA..., END) ตรวจ seq/ความยาว/CRC"""

    def __init__(self):
        self.total = None
        self.crc = None
        self.next_seq = 0
        self.buf = bytearray()

    def feed(self, packet: bytes):
        """คืน bytes ของข้อความเมื่อครบ (ได้รับ END) ไม่งั้นคืน None. error -> ProtocolError"""
        ptype, _msg_id, seq, payload = parse(packet)
        if ptype == NACK:
            code = payload[0] if payload else 0
            raise ProtocolError(ERROR_NAMES.get(code, f"NACK code {code}"))
        if ptype == START:
            if len(payload) < 6:
                raise ProtocolError("START สั้นเกินไป")
            self.total, self.crc = struct.unpack_from("<HI", payload)
            self.next_seq = 0
            self.buf = bytearray()
            return None
        if self.total is None:
            raise ProtocolError("ได้รับข้อมูลก่อน START")
        if ptype == DATA:
            if seq != self.next_seq:
                raise ProtocolError("ลำดับชิ้นข้อมูลผิด")
            self.buf += payload
            self.next_seq += 1
            return None
        if ptype == END:
            data = bytes(self.buf)
            if len(data) != self.total:
                raise ProtocolError("ข้อมูลไม่ครบ")
            if crc32(data) != self.crc:
                raise ProtocolError("CRC ไม่ตรง")
            return data
        raise ProtocolError(f"packet type ไม่รู้จัก 0x{ptype:02x}")
