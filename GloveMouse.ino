#include <Wire.h>
#include <BleMouse.h>

#include "Config.h"
#include "MouseTypes.h"
#include "MPU6050Driver.h"
#include "MotionProcessor.h"

// =============================================================================
// ออบเจกต์ส่วนกลาง (Global Instances)
// =============================================================================
MPU6050Driver   mpu;
MotionProcessor motion;
BleMouse        bleMouse("Glove Air Mouse", "ESP32", 100);

// FreeRTOS Handles
QueueHandle_t mouseQueue = nullptr;
TaskHandle_t  taskSensorHandle = nullptr;
TaskHandle_t  taskBleHandle = nullptr;
SemaphoreHandle_t MPUSemaphore;

void IRAM_ATTR NewDataFromMPU(){
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(MPUSemaphore, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// =============================================================================
// FreeRTOS Task 1: SENSOR & MOTION TASK (ทำงานบน Core 1)
// มีหน้าที่อ่านค่า MPU6050 และประมวลผลการเคลื่อนไหวทุกๆ 10ms เป๊ะๆ
// =============================================================================
void TaskSensor(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xPeriod = pdMS_TO_TICKS(Config::SENSOR_SAMPLE_RATE_MS);

  Serial.println("🟢 [TaskSensor] เริ่มทำงานบน Core " + String(xPortGetCoreID()));

  for (;;) {
    // กำหนดรอบการทำงานอย่างแม่นยำ (Deterministic Periodic Execution: 100 Hz)
    vTaskDelayUntil(&xLastWakeTime, xPeriod);

    if (xSemaphoreTake(MPUSemaphore, portMAX_DELAY) == pdTRUE){
      float gx, gy, gz;
      if (mpu.readGyro(gx, gy, gz)) {
        MousePacket packet = motion.process(gx, gy, gz, mpu);

        // ส่งข้อมูลเข้า Queue เมื่อมีการขยับหรือมีการกดปุ่ม
        if (packet.dx != 0 || packet.dy != 0 || packet.buttons != 0) {
          // ส่งเข้า Queue แบบ Non-blocking (ticksToWait = 0) หากคิวเต็มจะไม่ค้างรอ
          xQueueSend(mouseQueue, &packet, 0);
        }
      }
    }
    // [จุดต่อยอดในอนาคต]: เรียกฟังก์ชันอ่าน Flex Sensor หรือ Clutch Button ใน Task นี้
  }
}

// =============================================================================
// FreeRTOS Task 2: BLE MOUSE TASK (ทำงานบน Core 0)
// มีหน้าที่รับข้อมูลจาก Queue แล้วส่งสัญญาณ HID ไปยังคอมพิวเตอร์ผ่าน Bluetooth
// =============================================================================
void TaskBleMouse(void *pvParameters) {
  Serial.println("🔵 [TaskBleMouse] เริ่มทำงานบน Core " + String(xPortGetCoreID()));

  // เริ่มต้นบลูทูธบน Core 0 (แกนเดียวกับ BLE Stack ของ ESP32)
  bleMouse.begin();
  Serial.println("📡 [BLE] พร้อมเชื่อมต่อ! กรุณาเปิด Bluetooth เพื่อ Pair 'Glove Air Mouse'");

  MousePacket packet;

  for (;;) {
    // รอรับข้อมูลจาก Queue (จะ Block หลับไปจนกว่าจะมีข้อมูลใหม่เข้ามา จึงไม่เปลือง CPU)
    if (xQueueReceive(mouseQueue, &packet, portMAX_DELAY) == pdTRUE) {
      if (bleMouse.isConnected()) {
        // เลื่อนตำแหน่งเคอร์เซอร์
        if (packet.dx != 0 || packet.dy != 0) {
          bleMouse.move(packet.dx, packet.dy);
        }

        // [จุดต่อยอดในอนาคต]: สั่งคลิกเมาส์ตามสถานะ packet.buttons
      }
    }
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

  //ตั้งค่า interrupt จาก MPU
  MPUSemaphore = xSemaphoreCreateBinary();
  pinMode(Config::intterrupt_pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(Config::intterrupt_pin), NewDataFromMPU , RISING); //รับ intterrupt จาก MPU6050 
  enableInterrupt(4);

  // 2. เริ่มต้นและ Calibrate เซนเซอร์ MPU6050
  while (!mpu.begin(Config::MPU_DEFAULT_ADDR)) {
    delay(2000);
    Serial.println("กำลังลองเชื่อมต่อ MPU6050 ใหม่อีกครั้ง...");
  }
  mpu.calibrate();

  // 3. สร้าง FreeRTOS Queue สำหรับสื่อสารระหว่าง Task
  mouseQueue = xQueueCreate(Config::QUEUE_LENGTH, sizeof(MousePacket));
  if (mouseQueue == nullptr) {
    Serial.println("❌ ไม่สามารถสร้าง FreeRTOS Queue ได้!");
    while (1) { delay(1000); }
  }

  // 4. สร้าง FreeRTOS Tasks และแยกกระจายการทำงานลง Dual-Core
  // Core 1: ทำงานอ่านเซนเซอร์และประมวลผลการเคลื่อนไหว
  xTaskCreatePinnedToCore(
    TaskSensor,
    "SensorTask",
    Config::STACK_SENSOR_TASK,
    nullptr,
    Config::PRIORITY_SENSOR,
    &taskSensorHandle,
    Config::CORE_SENSOR_TASK
  );

  // Core 0: ทำงานบลูทูธและส่งข้อมูล HID
  xTaskCreatePinnedToCore(
    TaskBleMouse,
    "BleMouseTask",
    Config::STACK_BLE_TASK,
    nullptr,
    Config::PRIORITY_BLE,
    &taskBleHandle,
    Config::CORE_BLE_TASK
  );

  Serial.println("🚀 ระบบ FreeRTOS เริ่มต้นเสร็จสมบูรณ์!");
}

// =============================================================================
// LOOP (ปล่อยว่าง เนื่องจากงานทั้งหมดถูกส่งมอบให้ FreeRTOS Tasks แล้ว)
// =============================================================================
void loop() {
  // พัก loopTask หลัก เพื่อให้ทรัพยากรทั้งหมดไปอยู่ที่ RTOS Tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}
