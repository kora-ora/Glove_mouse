#include <Wire.h>

#include "Config.h"
#include "MouseTypes.h"
#include "MPU6050Driver.h"
#include "MotionProcessor.h"
#include "TouchClickManager.h"
#include "HidMouseService.h"
#include "TouchTapDetector.h"
#include "ClipboardService.h"
#include "Buzzer.h"
#include "Ds3231Rtc.h"
#include "OledStatusDisplay.h"
#include "SleepController.h"

// ==============================================================================
// 1. Hardware Instances & RTOS Handles
// ==============================================================================
MPU6050Driver     mpu;
MotionProcessor   motion;
TouchClickManager touchClick;
HidMouseService   hidMouse;
TouchTapDetector  touchTap;
ClipboardService  clipboard;
Ds3231Rtc         rtc;
Buzzer            buzzer;
OledStatusDisplay oled;
SleepController   sleepCtl;

QueueHandle_t     mouseQueue       = nullptr;
TaskHandle_t      taskSensorHandle = nullptr;
TaskHandle_t      taskBleHandle    = nullptr;
SemaphoreHandle_t mpuSemaphore     = nullptr;

// Debug Variables
volatile float   gDebugGx = 0, gDebugGy = 0, gDebugGz = 0;
volatile int8_t  gDebugDx = 0, gDebugDy = 0;
volatile uint8_t gDebugButtons = 0;
volatile bool    gRequestCalibration = false;

// ==============================================================================
// 2. Hardware Interrupts & Bus Recovery
// ==============================================================================
void IRAM_ATTR onMPUDataReady() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(mpuSemaphore, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void recoverI2CBus() {
  Wire.end();
  delay(10);
  Wire.begin(Config::PIN_SDA, Config::PIN_SCL);
  Wire.setClock(Config::I2C_CLOCK_SPEED);
}

// จัดการตัวนับและการกู้คืนเมื่อ I2C ล้มเหลว
void handleI2CError(bool success) {
  static uint8_t failStreak = 0;
  static bool recoveryAttempted = false;

  if (success) {
    failStreak = 0;
    recoveryAttempted = false;
    return;
  }

  if (++failStreak >= Config::I2C_FAIL_THRESHOLD && !recoveryAttempted) {
    Serial.println("⚠️ [I2C] พลาดต่อเนื่อง กำลัง reset bus...");
    recoverI2CBus();
    recoveryAttempted = true;
    failStreak = 0;
  } else if (failStreak >= Config::I2C_RESTART_THRESHOLD) {
    Serial.println("❌ [I2C] ยังพลาดต่อเนื่อง กำลัง restart บอร์ด...");
    Serial.flush();
    ESP.restart();
  }
}

// ==============================================================================
// 3. Helper Functions for Tasks
// ==============================================================================

// จัดการคำสั่ง Serial CLI เพื่อลดความซับซ้อนในลูปหลักของ BLE
void handleSerialCommands(uint8_t &taps) {
  while (Serial.available()) {
    char cmd = Serial.read();
    switch (cmd) {
      case 's':
        taps = 2;
        break;
      case 'c':
        taps = 4;
        break;
      case 'b':
        Serial.println(NimBLEDevice::deleteAllBonds() ? "🧹 [BLE] ล้าง bond แล้ว" : "⚠️ ล้าง bond ไม่สำเร็จ");
        break;
      case 't':
        Serial.printf("👆 [TOUCH] State(T7/P27)=%lu | Left(T8/P%d)=%lu | Right(T9/P%d)=%lu\n",
                      (unsigned long)touchTap.rawValue(),
                      Config::PIN_TOUCH_INDEX, (unsigned long)touchClick.rawIndex(),
                      Config::PIN_TOUCH_MIDDLE, (unsigned long)touchClick.rawMiddle());
        break;
      case 'g':
        Serial.printf("🕹️ [GYRO] gx=%.4f gy=%.4f gz=%.4f -> dx=%d dy=%d buttons=0x%02X paused=%s\n",
                      gDebugGx, gDebugGy, gDebugGz, gDebugDx, gDebugDy, gDebugButtons, 
                      hidMouse.isPaused() ? "ใช่" : "ไม่");
        break;
    }
  }
}

// จัดการ Action จากการแตะคำสั่งลัด (1x Pause/Resume, 2x Switch Host, 4x Calibrate)
void handleTapGestures(uint8_t taps) {
  if (taps == 1) {
    buzzer.beep(Config::BUZZER_TAP_MS);
    if (hidMouse.isPaused()) {
      hidMouse.resume();
    } else {
      hidMouse.pause();
    }
  } else if (taps == 2) {
    buzzer.beep(Config::BUZZER_SWITCH_MS);
    hidMouse.switchHost();
  } else if (taps == 4) {
    buzzer.beep(Config::BUZZER_SWITCH_MS);
    gRequestCalibration = true;
    Serial.println("🎯 [GESTURE] แตะ 4 ครั้ง -> ร้องขอ Calibrate Gyro ใหม่");
  }
}

// ==============================================================================
// 4. FreeRTOS Tasks
// ==============================================================================

// Task 1: Sensor & Motion (Core 1)
void TaskSensor(void *pvParameters) {
  uint8_t lastButtons = 0;
  Serial.println("🟢 [TaskSensor] ทำงานบน Core " + String(xPortGetCoreID()));

  for (;;) {
    // จัดการคำขอ Calibrate Gyro ใหม่ (จาก Gesture แตะ 4 ครั้ง หรือ Serial CLI)
    if (gRequestCalibration) {
      gRequestCalibration = false;
      oled.showCalibrating();
      buzzer.beep(Config::BUZZER_SWITCH_MS);
      mpu.calibrate();
      buzzer.beep(Config::BUZZER_TAP_MS);
      motion.resetScroll();
      continue;
    }

    // Guard clause: หากไม่มีสัญญาณ interrupt ให้ข้ามรอบไป
    if (xSemaphoreTake(mpuSemaphore, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    bool touchedNow = touchTap.isTouchedNow() || touchClick.isTouchedNow();

    // กรณีอยู่ในสถานะ Idle และไม่มีการแตะสัมผัส: ข้ามการประมวลผลทันที (Touch-Only Wake)
    if (sleepCtl.isIdle() && !touchedNow) {
      float dummyGx, dummyGy, dummyGz;
      mpu.readGyro(dummyGx, dummyGy, dummyGz);
      continue;
    }

    float gx, gy, gz;
    bool readSuccess = mpu.readGyro(gx, gy, gz);

    handleI2CError(readSuccess);
    if (!readSuccess) continue;

    MousePacket packet = motion.process(gx, gy, gz, mpu);
    packet.buttons |= touchClick.update();

    if (touchClick.isBothPressed()) {
      // โหมด 2-finger scroll: ล็อกตำแหน่งเคอร์เซอร์ X/Y และปุ่มคลิก แปลงการก้ม-เงย (gy) เป็นลูกกลิ้ง wheel
      packet.dx = 0;
      packet.dy = 0;
      packet.buttons = 0;
      packet.wheel = motion.processScroll(gy, mpu);
    } else {
      motion.resetScroll();
    }

    bool hasMoved = (packet.dx != 0 || packet.dy != 0 || packet.wheel != 0);

    // กรณีโหมดทำงานปกติ
    gDebugGx = gx; gDebugGy = gy; gDebugGz = gz;
    gDebugDx = packet.dx; gDebugDy = packet.dy;
    gDebugButtons = packet.buttons;

    oled.update(hidMouse.isConnected(), hidMouse.activeSlot(), hidMouse.isPaused(), packet);

    bool buttonsChanged = (packet.buttons != lastButtons);
    if (hasMoved || buttonsChanged) {
      if (xQueueSend(mouseQueue, &packet, 0) == pdTRUE) {
        lastButtons = packet.buttons;
      }
    }

    // Touch-Only Wake: wakeTrigger = touchedNow, keepAliveTrigger = hasMoved || buttonsChanged
    sleepCtl.tick(touchedNow, hasMoved || buttonsChanged);
  }
}

// Task 2: BLE Mouse & System Services (Core 0)
void TaskBleMouse(void *pvParameters) {
  Serial.println("🔵 [TaskBleMouse] ทำงานบน Core " + String(xPortGetCoreID()));

  hidMouse.begin("Glove Air Mouse", [](NimBLEServer *server) { clipboard.begin(server); });
  Serial.println("📡 [BLE] พร้อมเชื่อมต่อ 'Glove Air Mouse'");

  MousePacket packet;

  for (;;) {
    // ดึงและส่งข้อมูลเมาส์ออกจากคิวให้หมดเพื่อลด Latency
    if (xQueueReceive(mouseQueue, &packet, pdMS_TO_TICKS(10)) == pdTRUE) {
      do {
        hidMouse.send(packet);
      } while (xQueueReceive(mouseQueue, &packet, 0) == pdTRUE);
    }

    uint8_t taps = touchTap.update();
    handleSerialCommands(taps);
    handleTapGestures(taps);

    // บริการเบื้องหลัง
    hidMouse.service();
    clipboard.service();
    buzzer.service();
  }
}

// ==============================================================================
// 5. System Initialization
// ==============================================================================
void initHardware() {
  Wire.begin(Config::PIN_SDA, Config::PIN_SCL);
  Wire.setClock(Config::I2C_CLOCK_SPEED);

  rtc.begin();
  oled.begin(&rtc);
  oled.showCalibrating();

  while (!mpu.begin(Config::MPU_DEFAULT_ADDR)) {
    delay(2000);
    Serial.println("กำลังลองเชื่อมต่อ MPU6050 ใหม่...");
  }
  mpu.calibrate();
  mpu.enableInterrupt();

  touchClick.begin(Config::PIN_TOUCH_INDEX, Config::PIN_TOUCH_MIDDLE);
  touchTap.begin(Config::PIN_TOUCH);
  buzzer.begin(Config::PIN_BUZZER, Config::BUZZER_ACTIVE_HIGH);
  sleepCtl.begin(&oled);
}

void initRTOS() {
  mpuSemaphore = xSemaphoreCreateBinary();
  mouseQueue   = xQueueCreate(Config::QUEUE_LENGTH, sizeof(MousePacket));

  if (!mpuSemaphore || !mouseQueue) {
    Serial.println("❌ สร้าง RTOS Primitives ไม่สำเร็จ");
    while (1) { delay(1000); }
  }

  pinMode(Config::INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(Config::INTERRUPT_PIN), onMPUDataReady, RISING);

  xTaskCreatePinnedToCore(
    TaskSensor, "SensorTask", Config::STACK_SENSOR_TASK,
    nullptr, Config::PRIORITY_SENSOR, &taskSensorHandle, Config::CORE_SENSOR_TASK
  );

  xTaskCreatePinnedToCore(
    TaskBleMouse, "BleMouseTask", Config::STACK_BLE_TASK,
    nullptr, Config::PRIORITY_BLE, &taskBleHandle, Config::CORE_BLE_TASK
  );
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 FreeRTOS Glove Air Mouse ===");

  initHardware();
  initRTOS();

  Serial.println("🚀 ระบบ FreeRTOS เริ่มต้นเสร็จสมบูรณ์");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
