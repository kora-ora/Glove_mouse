#include <Wire.h>

#include "Config.h"
#include "MouseTypes.h"
#include "MPU6050Driver.h"
#include "MotionProcessor.h"
#include "FlexClickManager.h"
#include "HidMouseService.h"
#include "TouchTapDetector.h"
#include "ClipboardService.h"
#include "Buzzer.h"
#include "Ds3231Rtc.h"
#include "OledStatusDisplay.h"
#include "SleepController.h"

// =============================================================================
// ออบเจกต์ส่วนกลาง (Global Instances)
// =============================================================================
MPU6050Driver          mpu;
MotionProcessor        motion;
FlexClickManager       flexClick;
HidMouseService        hidMouse;
TouchTapDetector       touchTap;
ClipboardService       clipboard;
Ds3231Rtc              rtc;
Buzzer                 buzzer;
OledStatusDisplay      oled;
SleepController        sleepCtl;

// FreeRTOS Handles
QueueHandle_t     mouseQueue      = nullptr;
TaskHandle_t      taskSensorHandle = nullptr;
TaskHandle_t      taskBleHandle    = nullptr;
SemaphoreHandle_t mpuSemaphore    = nullptr;  // Binary Semaphore สำหรับสัญญาณ Interrupt จาก MPU6050

// ค่า debug ล่าสุดไว้ดูผ่านคำสั่ง 'g' ใน Serial Monitor (เขียนจาก TaskSensor, อ่านจาก TaskBleMouse)
volatile float gDebugGx = 0, gDebugGy = 0, gDebugGz = 0;
volatile int8_t gDebugDx = 0, gDebugDy = 0;
volatile uint8_t gDebugButtons = 0;  // ค่าปุ่ม (flex) ล่าสุด ไว้ดูผ่านคำสั่ง 'g' เทียบตอนแตะทัช (เช็คว่าทัชรบกวนขา flex หรือไม่)

// =============================================================================
// Interrupt Service Routine (ISR) - ต้องอยู่ใน IRAM เพื่อป้องกัน Crash
// เรียกทุกครั้งที่ MPU6050 มีข้อมูลใหม่พร้อม (ขา INT ส่งสัญญาณ RISING)
// =============================================================================
void IRAM_ATTR onMPUDataReady() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(mpuSemaphore, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// =============================================================================
// I2C Recovery: reset บัส I2C เมื่ออ่าน/เขียนพลาดติดกันหลายครั้ง
// =============================================================================
void recoverI2CBus() {
  Wire.end();
  delay(10);
  Wire.begin(Config::PIN_SDA, Config::PIN_SCL);
  Wire.setClock(Config::I2C_CLOCK_SPEED);
}

// =============================================================================
// FreeRTOS Task 1: SENSOR & MOTION TASK (ทำงานบน Core 1)
// ทำงานแบบ Event-Driven: ตื่นขึ้นเมื่อ ISR ส่ง Semaphore (มีข้อมูลใหม่จาก MPU6050)
// =============================================================================
void TaskSensor(void *pvParameters) {
  uint8_t lastButtons = 0;
  uint8_t i2cFailStreak = 0;
  bool recoveryAttempted = false;
  Serial.println("🟢 [TaskSensor] เริ่มทำงานบน Core " + String(xPortGetCoreID()));

  for (;;) {
    // หลับรอจนกว่า MPU6050 จะส่ง Interrupt แจ้งว่ามีข้อมูลใหม่
    // (portMAX_DELAY = รอไม่มีกำหนด ไม่เปลือง CPU ระหว่างรอ)
    if (xSemaphoreTake(mpuSemaphore, portMAX_DELAY) == pdTRUE) {
      bool touchedNow = touchTap.isTouchedNow();
      bool cycleOk = true;

      if (sleepCtl.isIdle() && !touchedNow) {
        // Idle: งดอ่าน gyro/flex, งดวาด OLED, งดส่ง BLE queue ประหยัด I2C/CPU/BLE traffic
        sleepCtl.tick(false, false);
      } else {
        MousePacket packet = {0, 0, 0};

        // อ่านค่า Gyroscope และแปลงเป็นระยะขยับเมาส์
        float gx, gy, gz;
        if (mpu.readGyro(gx, gy, gz)) {
          packet = motion.process(gx, gy, gz, mpu);
          gDebugGx = gx; gDebugGy = gy; gDebugGz = gz;
          gDebugDx = packet.dx; gDebugDy = packet.dy;
        } else {
          cycleOk = false;
        }

        // ตรวจสถานะ Flex Sensor (คลิกซ้าย/ขวา)
        packet.buttons |= flexClick.update();
        gDebugButtons = packet.buttons;

        // I2C ถูกใช้งานจาก TaskSensor เพียง task เดียวหลัง setup จึงไม่ชนกับการอ่าน MPU6050
        oled.update(hidMouse.isConnected(), hidMouse.activeSlot(), packet);

        bool buttonsChanged = packet.buttons != lastButtons;
        // ส่งข้อมูลเข้า Queue เมื่อมีการขยับ หรือสถานะปุ่มเปลี่ยน (รวมตอนปล่อยปุ่มด้วย)
        if (packet.dx != 0 || packet.dy != 0 || buttonsChanged) {
          xQueueSend(mouseQueue, &packet, 0);  // Non-blocking: ถ้าคิวเต็มจะข้ามไป
          lastButtons = packet.buttons;
        }

        sleepCtl.tick(packet.dx != 0 || packet.dy != 0 || buttonsChanged, touchedNow);
      }

      // นับความล้มเหลวของ I2C ต่อเนื่อง -> reset bus -> ถ้ายังพลาดต่อ -> restart บอร์ด
      if (cycleOk) {
        i2cFailStreak = 0;
        recoveryAttempted = false;
      } else if (++i2cFailStreak >= Config::I2C_FAIL_THRESHOLD) {
        if (!recoveryAttempted) {
          Serial.println("⚠️ [I2C] อ่านพลาดติดกันหลายครั้ง กำลัง reset bus...");
          recoverI2CBus();
          recoveryAttempted = true;
          i2cFailStreak = 0;
        } else if (i2cFailStreak >= Config::I2C_RESTART_THRESHOLD) {
          Serial.println("❌ [I2C] ยังพลาดต่อเนื่องหลัง reset bus แล้ว กำลัง restart บอร์ด...");
          Serial.flush();
          ESP.restart();
        }
      }
    }
  }
}

// =============================================================================
// FreeRTOS Task 2: BLE MOUSE TASK (ทำงานบน Core 0)
// มีหน้าที่รับข้อมูลจาก Queue แล้วส่งสัญญาณ HID ไปยังคอมพิวเตอร์ผ่าน Bluetooth
// =============================================================================
void TaskBleMouse(void *pvParameters) {
  Serial.println("🔵 [TaskBleMouse] เริ่มทำงานบน Core " + String(xPortGetCoreID()));

  // เริ่มต้นบลูทูธบน Core 0 (แกนเดียวกับ BLE Stack ของ ESP32)
  hidMouse.begin("Glove Air Mouse", [](NimBLEServer *server) { clipboard.begin(server); });
  Serial.println("📡 [BLE] พร้อมเชื่อมต่อ! กรุณาเปิด Bluetooth เพื่อ Pair 'Glove Air Mouse' (ต่อได้ 2 เครื่อง)");

  MousePacket packet;

  for (;;) {
    // รอ Queue สั้นๆ เพื่อให้ได้ตรวจทัชเป็นระยะแม้ไม่มีการขยับเมาส์
    // (packet.buttons เป็นสถานะปุ่มปัจจุบัน ส่งเป็น HID report เต็มทุกครั้ง)
    if (xQueueReceive(mouseQueue, &packet, pdMS_TO_TICKS(10)) == pdTRUE) {
      hidMouse.send(packet);
    }

    // ทัช: แตะ 1 ครั้ง = สลับ ทำงาน/หยุด | แตะ 2 ครั้ง = สลับเครื่อง A<->B
    // Serial Monitor: 's' = เหมือนแตะ 2 ครั้ง, 't' = พิมพ์ค่า touchRead (ไว้ปรับ TOUCH_THRESHOLD),
    // 'b' = ล้าง bond ทั้งหมดในบอร์ด,
    // 'g' = พิมพ์ค่า gyro ดิบ/dx,dy ล่าสุด + สถานะ paused (debug ตอนเมาส์ไม่ขยับ)
    uint8_t taps = touchTap.update();
    while (Serial.available()) {
      char cmd = Serial.read();
      if (cmd == 's') {
        taps = 2;
      } else if (cmd == 'b') {
        Serial.println(NimBLEDevice::deleteAllBonds() ? "🧹 [BLE] ล้าง bond ในบอร์ดแล้ว (ลบอุปกรณ์ใน Windows แล้ว pair ใหม่)"
                                                      : "⚠️ [BLE] ล้าง bond ไม่สำเร็จ");
      } else if (cmd == 't') {
        Serial.printf("👆 [TOUCH] value=%lu (แตะ = ต่ำกว่า %lu)\n",
                      (unsigned long)touchTap.rawValue(), (unsigned long)Config::TOUCH_THRESHOLD);
      } else if (cmd == 'g') {
        Serial.printf("🕹️ [GYRO] gx=%.4f gy=%.4f gz=%.4f rad/s -> dx=%d dy=%d buttons=0x%02X paused=%s\n",
                      gDebugGx, gDebugGy, gDebugGz, gDebugDx, gDebugDy, gDebugButtons, hidMouse.isPaused() ? "ใช่" : "ไม่");
      }
    }
    // ทุก gesture ที่แตะติดจริงจะบี๊บให้รู้ตัวเสมอ (สั้น/กลาง ต่างกันตามจำนวนครั้ง) แม้ผลลัพธ์จะทำไม่สำเร็จก็ตาม
    // (เช่นแตะ 2 ครั้งตอนมีเครื่องเดียว) เพื่อยืนยันว่าทัชจับการแตะได้ แยกจากปัญหาเรื่องผลลัพธ์
    if (taps == 1) {
      buzzer.beep(Config::BUZZER_TAP_MS);
      if (hidMouse.isPaused()) hidMouse.resume(); else hidMouse.pause();
    } else if (taps >= 2) {
      buzzer.beep(Config::BUZZER_SWITCH_MS);
      hidMouse.switchHost();
    }

    hidMouse.service();
    clipboard.service();
    buzzer.service();
  }
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n==============================================");
  Serial.println("   ESP32 FreeRTOS Glove Air Mouse System      ");
  Serial.println("==============================================");

  // 1. เริ่มต้นระบบ I2C Bus
  Wire.begin(Config::PIN_SDA, Config::PIN_SCL);
  Wire.setClock(Config::I2C_CLOCK_SPEED);

  rtc.begin();
  // OLED ใช้ I2C bus เดียวกับ MPU6050: แสดงระหว่างการคาลิเบรต
  oled.begin(&rtc);
  oled.showCalibrating();

  // 2. เริ่มต้นและ Calibrate เซนเซอร์ MPU6050
  while (!mpu.begin(Config::MPU_DEFAULT_ADDR)) {
    delay(2000);
    Serial.println("กำลังลองเชื่อมต่อ MPU6050 ใหม่อีกครั้ง...");
  }
  mpu.calibrate();
  // เปิดสัญญาณ Interrupt บนชิป MPU6050 (สั่งให้ขา INT ส่งสัญญาณเมื่อมีข้อมูลใหม่)
  mpu.enableInterrupt();

  // 3. ตั้งค่า Interrupt Pin และสร้าง Semaphore
  // (ต้องสร้าง Semaphore ก่อน attachInterrupt เสมอ เพื่อป้องกัน ISR เรียก NULL Handle)
  mpuSemaphore = xSemaphoreCreateBinary();
  if (mpuSemaphore == nullptr) {
    Serial.println("❌ ไม่สามารถสร้าง mpuSemaphore ได้!");
    while (1) { delay(1000); }
  }
  pinMode(Config::INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(Config::INTERRUPT_PIN), onMPUDataReady, RISING);

  // 4. เริ่มต้นและ Calibrate Flex Sensor (วางมือแบนนิ่งไว้ขณะ boot)
  flexClick.begin(Config::PIN_FLEX_INDEX, Config::PIN_FLEX_MIDDLE);
  flexClick.calibrate();

  // 5. Capacitive touch (GPIO 27 = T7)
  touchTap.begin(Config::PIN_TOUCH);
  buzzer.begin(Config::PIN_BUZZER, Config::BUZZER_ACTIVE_HIGH);

  // 5.1 Idle mode
  sleepCtl.begin(&oled);

  // 5.2 สร้าง FreeRTOS Queue สำหรับสื่อสารระหว่าง Task
  mouseQueue = xQueueCreate(Config::QUEUE_LENGTH, sizeof(MousePacket));
  if (mouseQueue == nullptr) {
    Serial.println("❌ ไม่สามารถสร้าง FreeRTOS Queue ได้!");
    while (1) { delay(1000); }
  }

  // 6. สร้าง FreeRTOS Tasks และแยกกระจายการทำงานลง Dual-Core
  xTaskCreatePinnedToCore(
    TaskSensor,
    "SensorTask",
    Config::STACK_SENSOR_TASK,
    nullptr,
    Config::PRIORITY_SENSOR,
    &taskSensorHandle,
    Config::CORE_SENSOR_TASK   // Core 1: ทำงานอ่านเซนเซอร์และประมวลผลการเคลื่อนไหว
  );

  xTaskCreatePinnedToCore(
    TaskBleMouse,
    "BleMouseTask",
    Config::STACK_BLE_TASK,
    nullptr,
    Config::PRIORITY_BLE,
    &taskBleHandle,
    Config::CORE_BLE_TASK      // Core 0: ทำงานบลูทูธและส่งข้อมูล HID
  );

  Serial.println("🚀 ระบบ FreeRTOS เริ่มต้นเสร็จสมบูรณ์!");
}

// =============================================================================
// LOOP (ปล่อยว่าง เนื่องจากงานทั้งหมดถูกส่งมอบให้ FreeRTOS Tasks แล้ว)
// =============================================================================
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
