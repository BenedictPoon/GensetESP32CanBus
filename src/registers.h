#pragma once

#include <Arduino.h>
#include <cstdint>

// CCMODBUS register image (HIMOINSA CCMODBUS_En.pdf).
// Values are filled by CEx7 CAN decode when mapping is known; otherwise 0.

static constexpr size_t kCoilCount          = 16;
static constexpr size_t kDiscreteCount      = 64;
static constexpr size_t kInputRegisterCount = 64;
static constexpr size_t kHoldingCount       = 128;
static constexpr size_t kJ1939RegisterBase  = 600;
static constexpr size_t kJ1939RegisterCount = 40;

struct GensetRegisters {
  bool coils[kCoilCount];
  bool discrete[kDiscreteCount];
  uint16_t inputRegs[kInputRegisterCount];
  uint16_t holding[kHoldingCount];
  uint16_t j1939[kJ1939RegisterCount];

  bool fuelValid;
  uint32_t lastFuelUpdateMs;
  bool batteryValid;
  uint32_t lastBatteryUpdateMs;
  bool engineHoursValid;
  uint32_t lastEngineHoursUpdateMs;
  uint32_t canRxCount;
  uint32_t canUniqueIds;
};

void registersInit(GensetRegisters& regs);
void registersSetFuelPermille(GensetRegisters& regs, uint16_t permille);
float registersFuelPercent(const GensetRegisters& regs);
void registersSetBatteryDv(GensetRegisters& regs, uint16_t deciVolts);
void registersSetEngineHoursFromMinutes(GensetRegisters& regs, uint32_t totalMinutes);

// Named PDU addresses (Table 6 / coils / discrete)
namespace RegPdu {
  // Coils
  constexpr uint16_t kCoilFuelTransferPump = 7;

  // Discrete / input status
  constexpr uint16_t kDiscreteFuelReserve  = 6;
  constexpr uint16_t kDiscreteLowFuelLevel = 18;

  // Input registers
  constexpr uint16_t kIrFuelLevel          = 26;  // ‰
  constexpr uint16_t kIrSpeedRpm           = 25;
  constexpr uint16_t kIrBattAltDv          = 27;  // battery charging alternator, dV
  constexpr uint16_t kIrBatteryDv          = 28;  // start battery voltage, dV
  constexpr uint16_t kIrSensorPresence     = 32;  // bit0 = fuel level sensor
  constexpr uint16_t kIrEngineHoursHh      = 41;  // total engine hours (hh)
  constexpr uint16_t kIrEngineHoursMmSs    = 42;  // LSB=sec, MSB=min

  // Holding (thresholds)
  constexpr uint16_t kHrFuelPumpMin        = 31;
  constexpr uint16_t kHrFuelPumpMax        = 32;
  constexpr uint16_t kHrLowFuelLevel       = 38;

  // Device settings (Table 10)
  constexpr uint16_t kHrModbusId           = 100;
  constexpr uint16_t kHrBaudrate           = 101;
  constexpr uint16_t kHrParity             = 102;
}
