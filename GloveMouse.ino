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

// Instances
MPU6050Driver     mpu;
MotionProcessor   motion;
FlexClickManager  flexClick;
HidMouseService   hidMouse;
TouchTapDetector  touchTap;
ClipboardService  clipboard;
Ds3231Rtc         rtc;
Buzzer            buzzer;
OledStatusDisplay oled;
SleepController   sleepCtl;

// FreeRTOS Handles
QueueHandle_t     mouseQueue       = nullptr;
TaskHandle_t      taskSensorHandle = nullptr;
TaskHandle_t      taskBleHandle    = nullptr;
SemaphoreHandle_t mpuSemaphore     = nullptr;

// Debug variables
volatile float   gDebugGx = 0, gDebugGy = 0, gDebugGz = 0;
volatile int8_t  gDebugDx = 0, gDebugDy = 0;
volatile uint8_t gDebugButtons = 0;

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

// Task 1: Sensor & Motion (Core 1)
void TaskSensor(void *pvParameters) {
  uint8_t lastButtons = 0;
  uint8_t i2cFailStreak = 0;
  bool recoveryAttempted = false;
  Serial.println("🟢 [TaskSensor] ทำงานบน Core " + String(xPortGetCoreID()));

  for (;;) {
    if (xSemaphoreTake(mpuSemaphore, portMAX_DELAY) == pdTRUE) {
      bool touchedNow = touchTap.isTouchedNow();
      bool cycleOk = true;

      if (sleepCtl.isIdle() && !touchedNow) {
        sleepCtl.tick(false, false);
      } else {
        MousePacket packet = {0, 0, 0};

        float gx, gy, gz;
        if (mpu.readGyro(gx, gy, gz)) {
          packet = motion.process(gx, gy, gz, mpu);
          gDebugGx = gx; gDebugGy = gy; gDebugGz = gz;
          gDebugDx = packet.dx; gDebugDy = packet.dy;
        } else {
          cycleOk = false;
        }

        packet.buttons |= flexClick.update();
        gDebugButtons = packet.buttons;

        oled.update(hidMouse.isConnected(), hidMouse.activeSlot(), packet);

        bool buttonsChanged = (packet.buttons != lastButtons);
        if (packet.dx != 0 || packet.dy != 0 || buttonsChanged) {
          xQueueSend(mouseQueue, &packet, 0);
          lastButtons = packet.buttons;
        }

        sleepCtl.tick(packet.dx != 0 || packet.dy != 0 || buttonsChanged, touchedNow);
      }

      if (cycleOk) {
        i2cFailStreak = 0;
        recoveryAttempted = false;
      } else if (++i2cFailStreak >= Config::I2C_FAIL_THRESHOLD) {
        if (!recoveryAttempted) {
          Serial.println("⚠️ [I2C] พลาดต่อเนื่อง กำลัง reset bus...");
          recoverI2CBus();
          recoveryAttempted = true;
          i2cFailStreak = 0;
        } else if (i2cFailStreak >= Config::I2C_RESTART_THRESHOLD) {
          Serial.println("❌ [I2C] ยังพลาดต่อเนื่อง กำลัง restart บอร์ด...");
          Serial.flush();
          ESP.restart();
        }
      }
    }
  }
}

// Task 2: BLE Mouse (Core 0)
void TaskBleMouse(void *pvParameters) {
  Serial.println("🔵 [TaskBleMouse] ทำงานบน Core " + String(xPortGetCoreID()));

  hidMouse.begin("Glove Air Mouse", [](NimBLEServer *server) { clipboard.begin(server); });
  Serial.println("📡 [BLE] พร้อมเชื่อมต่อ 'Glove Air Mouse'");

  MousePacket packet;

  for (;;) {
    if (xQueueReceive(mouseQueue, &packet, pdMS_TO_TICKS(10)) == pdTRUE) {
      hidMouse.send(packet);
    }

    uint8_t taps = touchTap.update();
    while (Serial.available()) {
      char cmd = Serial.read();
      if (cmd == 's') {
        taps = 2;
      } else if (cmd == 'b') {
        Serial.println(NimBLEDevice::deleteAllBonds() ? "🧹 [BLE] ล้าง bond แล้ว" : "⚠️ ล้าง bond ไม่สำเร็จ");
      } else if (cmd == 't') {
        Serial.printf("👆 [TOUCH] value=%lu\n", (unsigned long)touchTap.rawValue());
      } else if (cmd == 'g') {
        Serial.printf("🕹️ [GYRO] gx=%.4f gy=%.4f gz=%.4f -> dx=%d dy=%d buttons=0x%02X paused=%s\n",
                      gDebugGx, gDebugGy, gDebugGz, gDebugDx, gDebugDy, gDebugButtons, hidMouse.isPaused() ? "ใช่" : "ไม่");
      }
    }

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

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== ESP32 FreeRTOS Glove Air Mouse ===");

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

  mpuSemaphore = xSemaphoreCreateBinary();
  if (mpuSemaphore == nullptr) {
    Serial.println("❌ สร้าง mpuSemaphore ไม่สำเร็จ");
    while (1) { delay(1000); }
  }
  pinMode(Config::INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(Config::INTERRUPT_PIN), onMPUDataReady, RISING);

  flexClick.begin(Config::PIN_FLEX_INDEX, Config::PIN_FLEX_MIDDLE);
  flexClick.calibrate();

  touchTap.begin(Config::PIN_TOUCH);
  buzzer.begin(Config::PIN_BUZZER, Config::BUZZER_ACTIVE_HIGH);

  sleepCtl.begin(&oled);

  mouseQueue = xQueueCreate(Config::QUEUE_LENGTH, sizeof(MousePacket));
  if (mouseQueue == nullptr) {
    Serial.println("❌ สร้าง mouseQueue ไม่สำเร็จ");
    while (1) { delay(1000); }
  }

  xTaskCreatePinnedToCore(
    TaskSensor, "SensorTask", Config::STACK_SENSOR_TASK,
    nullptr, Config::PRIORITY_SENSOR, &taskSensorHandle, Config::CORE_SENSOR_TASK
  );

  xTaskCreatePinnedToCore(
    TaskBleMouse, "BleMouseTask", Config::STACK_BLE_TASK,
    nullptr, Config::PRIORITY_BLE, &taskBleHandle, Config::CORE_BLE_TASK
  );

  Serial.println("🚀 ระบบ FreeRTOS เริ่มต้นเสร็จสมบูรณ์");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
