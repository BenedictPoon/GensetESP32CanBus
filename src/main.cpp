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

  Serial.println(F("Ready. Poll Modbus FC04 PDU addr 26 for fuel (‰)."));
  Serial.println(F("Fuel mapped from CAN ID 0x0201FF05 (u16 LE @ byte 0 → ‰)."));
  Serial.println();
}

void loop() {
  // Drain CAN RX queue
  CanFrame frame;
  while (canTwaiReceive(frame)) {
    cex7CanOnFrame(gCex7, frame, gRegs);
  }
  canTwaiTask();

  // Keep Modbus image in sync with decoded registers
  modbusSlaveSyncFromRegisters(gRegs);
  modbusSlaveTask();

  cex7CanPrintStats(gCex7, gRegs);
}
