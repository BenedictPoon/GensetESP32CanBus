#include <Arduino.h>

#include "can_twai.h"
#include "cex7_can.h"
#include "config.h"
#include "modbus_slave.h"
#include "registers.h"

static GensetRegisters gRegs;
static Cex7CanState gCex7;

void setup() {
  Serial.begin(kSerialBaud);
  delay(800);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F(" ESP32 CCMODBUS Replacement (CEx7 CAN)"));
  Serial.println(F(" Waveshare ESP32-S3-RS485-CAN"));
  Serial.println(F("========================================"));

  registersInit(gRegs);
  cex7CanInit(gCex7);

  if (!canTwaiBegin()) {
    Serial.println(F("[FATAL] CAN init failed"));
  }

  if (!modbusSlaveBegin(gRegs)) {
    Serial.println(F("[FATAL] Modbus slave init failed"));
  }

  modbusSlaveSyncFromRegisters(gRegs);

  Serial.println(F("Ready. Poll Modbus FC04 / FC01:"));
  Serial.println(F("  IR 26 fuel ‰ | IR 28 batt dV | IR 25 rpm"));
  Serial.println(F("  FC01 addr 1 qty 2 → [Start, Stop]  (F320 bit6, not RPM)"));
  Serial.println(F("  IR 41/42 engine hours"));
  Serial.println(F("CAN: FF05 fuel/batt/rpm | FF13 hours | F320/FF20 Start/Stop"));
  Serial.println();
}

void loop() {
  // Drain CAN RX; service Modbus between frames so polls are not delayed
  CanFrame frame;
  while (canTwaiReceive(frame)) {
    cex7CanOnFrame(gCex7, frame, gRegs);
    modbusSlaveTask();
  }
  canTwaiTask();

  modbusSlaveSyncLive(gRegs);
  modbusSlaveTask();

  cex7CanPrintStats(gCex7, gRegs);
}
