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
  regs.batteryValid = false;
  regs.engineHoursValid = false;
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

void registersSetBatteryDv(GensetRegisters& regs, uint16_t deciVolts) {
  // CCMODBUS Input Register PDU 28: battery voltage in dV (12.3 V → 123)
  regs.inputRegs[RegPdu::kIrBatteryDv] = deciVolts;
  regs.batteryValid = true;
  regs.lastBatteryUpdateMs = millis();
}

void registersSetEngineHoursFromMinutes(GensetRegisters& regs, uint32_t totalMinutes) {
  // CCMODBUS: IR 41 = hours (hh), IR 42 = mm:ss (LSB=sec, MSB=min)
  const uint32_t hours = totalMinutes / 60u;
  const uint32_t remMin = totalMinutes % 60u;
  const uint16_t hh = hours > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(hours);
  const uint16_t mmSs = static_cast<uint16_t>((remMin & 0xFFu) << 8);  // sec=0

  regs.inputRegs[RegPdu::kIrEngineHoursHh] = hh;
  regs.inputRegs[RegPdu::kIrEngineHoursMmSs] = mmSs;
  regs.engineHoursValid = true;
  regs.lastEngineHoursUpdateMs = millis();
}
