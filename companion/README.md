# Glove Clipboard (companion app)

โปรแกรมถาดระบบบน Windows ที่ sync ข้อความ clipboard ระหว่าง 2 เครื่องผ่านถุงมือ (ESP32)
Ctrl+C เครื่องหนึ่ง แล้ว Ctrl+V อีกเครื่องได้เลย ใช้ได้เฉพาะข้อความล้วน (รวมภาษาไทย/URL/โค้ดสั้นๆ ไม่เกิน 16384 ไบต์ ≈ ภาษาไทย 5,400 ตัวอักษร)

## ติดตั้ง (ทำทั้ง 2 เครื่อง)
```bash
pip install -r requirements.txt
```

## ใช้งาน
1. flash เฟิร์มแวร์ล่าสุดและ pair ถุงมือกับทั้งสองเครื่อง (ลบ "Glove Air Mouse" เก่าใน Windows ก่อน ถ้าเห็น service ไม่ครบ เพราะ Windows แคช GATT)
2. รันบนทั้งสองเครื่อง:
```bash
python app.py
```
   ไอคอนที่ถาดระบบ: เขียว = ต่อถุงมือแล้ว, เหลือง = กำลังต่อ, เทา = ยังไม่ต่อ, แดง = หยุดชั่วคราว
3. Ctrl+C บนเครื่อง A → เครื่อง B ได้รับและใส่ clipboard ให้เอง (มีแจ้งเตือน) แล้ว Ctrl+V

app หา address ของถุงมือเอง เพราะถุงมือที่ต่อ Windows อยู่จะไม่ advertise ทำให้สแกนหาชื่อไม่เจอ ลำดับการหา: `--address` → รายการอุปกรณ์ที่ pair ไว้ใน Windows → สแกนหาชื่อ → `FALLBACK_ADDRESS` ใน `app.py` (Bluetooth MAC ผูกกับชิป ESP32 ไม่เปลี่ยนตามการอัปโหลดเฟิร์มแวร์; ดู address ได้จาก Serial ตอนบูต `Address: ...`). ระบุเองได้:
```bash
python app.py --address AA:BB:CC:DD:EE:FF
```

## กลไกที่ควรรู้
- **กันลูป:** ตอนแอปเขียน clipboard เอง จะจำหมายเลข sequence ไว้ ไม่นับเป็นการ copy ใหม่ และข้ามข้อความที่ CRC เท่ากับที่เพิ่งส่ง/รับ
- **ข้าม:** ไฟล์, รูป, ข้อความว่าง, และข้อมูลที่ password manager ตั้งธง `ExcludeClipboardContentFromMonitorProcessing` หรือ `CanIncludeInClipboardHistory = 0`
- ข้อความบนถุงมือถูกล้างเองหลัง 60 วินาที
- เก็บได้ 1 ข้อความ ข้อความใหม่ทับของเก่า
