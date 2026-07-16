#include "registers.h"

#include <cstring>

#include "config.h"

void registersInit(GensetRegisters& regs) {
  memset(&regs, 0, sizeof(regs));

  // Device settings mirror CCMODBUS defaults
  regs.holding[RegPdu::kHrModbusId] = kModbusSlaveId;
  regs.holding[RegPdu::kHrBaudrate] = 0;  // 0 = 9600
  regs.holding[RegPdu::kHrParity]   = 0;  // 0 = NONE

  // Reasonable placeholder thresholds (writable later when CAN TX known)
  regs.holding[RegPdu::kHrFuelPumpMin]  = 20;  // 5–90%
  regs.holding[RegPdu::kHrFuelPumpMax]  = 80;  // 10–100%
  regs.holding[RegPdu::kHrLowFuelLevel] = 10;  // 0–30

  regs.fuelValid = false;
}

void registersSetFuelPermille(GensetRegisters& regs, uint16_t permille) {
  if (permille > 1000) {
    permille = 1000;
  }
  regs.inputRegs[RegPdu::kIrFuelLevel] = permille;
  regs.inputRegs[RegPdu::kIrSensorPresence] |= 0x0001;  // fuel sensor present
  regs.fuelValid = true;
  regs.lastFuelUpdateMs = millis();

  // Derive discrete alarms from level (‰ → compare to holding % thresholds)
  const uint16_t percent = permille / 10;
  regs.discrete[RegPdu::kDiscreteLowFuelLevel] =
      (percent <= regs.holding[RegPdu::kHrLowFuelLevel]);
  // Fuel reserve often slightly above critical; use same threshold for now
  regs.discrete[RegPdu::kDiscreteFuelReserve] =
      (percent <= regs.holding[RegPdu::kHrLowFuelLevel]);
}

float registersFuelPercent(const GensetRegisters& regs) {
  return regs.inputRegs[RegPdu::kIrFuelLevel] / 10.0f;
}
