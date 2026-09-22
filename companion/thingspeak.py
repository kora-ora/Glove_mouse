"""ส่ง event ขึ้น ThingSpeak (cloud IoT platform) ทีละ HTTP GET แบบ blocking
เรียกจาก ThingSpeakWorker (เธรดแยก) เท่านั้น ห้ามเรียกตรงจาก event loop หลัก เพราะ urlopen เป็น blocking call

field ที่ใช้ในโปรเจกต์นี้ (channel เดียวใช้ร่วมกันได้ทั้งสองเครื่อง):
  field1 = ความยาวข้อความที่ sync (ตัวอักษร)
  field2 = (ยังไม่ได้ใช้) เครื่อง active สำหรับควบคุมเมาส์ (A/B) — ต้องแก้เฟิร์มแวร์ให้ส่งสถานะนี้ผ่าน BLE มาก่อน
            companion app เห็นแค่การเชื่อมต่อ/clipboard ของตัวเอง ไม่รู้ว่าเครื่องไหนเป็น active host ของเมาส์
  field3 = (ยังไม่ได้ใช้) จำนวนครั้งที่แตะสลับ host ต่อวัน — เป็น event ฝั่งเฟิร์มแวร์เหมือนกัน ต้องส่งผ่าน BLE มาก่อน
  field4 = จำนวนครั้งที่ลิงก์ BLE ต่อใหม่สะสม (นับตั้งแต่เปิดแอป) ไว้ดูความเสถียรของ BLE ย้อนหลังเป็นกราฟ
  field5 = เวลาตั้งแต่เริ่มส่งจนถุงมือ ACK กลับ (ms) — วัด "BLE round-trip ไปถุงมือ" ไม่ใช่เวลาที่อีกเครื่องได้รับจริง
           (เวลาที่อีกเครื่องได้รับจริงต้องมีนาฬิการ่วมกันหรือฝัง timestamp ในโปรโตคอล ยังไม่ได้ทำ)

สมัคร/สร้าง channel และขอ Write API Key ได้ที่ https://thingspeak.com (Add Channel -> API Keys)
บัญชีฟรีจำกัดอัปเดตไม่เกิน 1 ครั้งทุก 15 วินาทีต่อ channel จึงจำกัดอัตราไว้ในตัวโมดูลนี้เลย
"""
import time
import urllib.parse
import urllib.request

MIN_INTERVAL_S = 15.0  # ตามข้อจำกัดของ ThingSpeak free tier

_last_sent = 0.0


def log_event(api_key: str, **fields) -> bool:
    """ส่ง field ที่ระบุ (เช่น field1=5, field2=1) ไปยัง channel ของ api_key นี้
    คืน True ถ้าส่งจริง (False = ข้ามเพราะถี่เกินไป/ไม่มี key/ไม่มี field)
    """
    global _last_sent
    if not api_key or not fields:
        return False
    now = time.monotonic()
    if now - _last_sent < MIN_INTERVAL_S:
        return False
    _last_sent = now

    params = {"api_key": api_key, **fields}
    url = "https://api.thingspeak.com/update?" + urllib.parse.urlencode(params)
    try:
        urllib.request.urlopen(url, timeout=5).close()
    except Exception:
        pass  # เน็ต/cloud มีปัญหาไม่ควรทำให้ clipboard sync ใช้งานไม่ได้ ข้ามเงียบๆ
    return True
