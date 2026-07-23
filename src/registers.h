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
  // Serial + Modbus: panel Cool Down while Start still 1 (coil PDU 3)
  bool coolDown;
  // Cool-down countdown from 0x0201FF14 byte4 (IR PDU 29 when valid)
  uint8_t coolDownTimer;
  bool coolDownTimerValid;
  uint32_t canRxCount;
  uint32_t canUniqueIds;
};

void registersInit(GensetRegisters& regs);
void registersSetFuelPermille(GensetRegisters& regs, uint16_t permille);
float registersFuelPercent(const GensetRegisters& regs);
void registersSetBatteryDv(GensetRegisters& regs, uint16_t deciVolts);
void registersSetEngineHoursFromMinutes(GensetRegisters& regs, uint32_t totalMinutes);
void registersSetEngineRpm(GensetRegisters& regs, uint16_t rpm);
// Start/Stop coils from CAN status bit (not RPM).
void registersSetRunState(GensetRegisters& regs, bool operating);
// Auto/Manual from F320 byte0 bit3/bit2 — does not touch Start/Stop.
void registersSetControlMode(GensetRegisters& regs, bool automatic, bool manual);
// CoolDown — does not touch Start/Stop coils.
void registersSetCoolDown(GensetRegisters& regs, bool coolDown);
void registersSetCoolDownTimer(GensetRegisters& regs, uint8_t secondsRemaining);

// Named PDU addresses (Table 6 / coils / discrete)
namespace RegPdu {
  // Coils — live read order for Node-RED qty 5 from addr 1:
  //   [Start, Stop, CoolDown, Automatic, Manual]
  // CoolDown stays on PDU 3 with Start/Stop (unchanged decode). Auto/Manual are
  // additional on 4/5 (same F320 bits as CCMODBUS Auto/Manual; PDF put those on 3/4).
  constexpr uint16_t kCoilReset            = 0;
  constexpr uint16_t kCoilStartUp          = 1;  // 1 = genset operating (read)
  constexpr uint16_t kCoilStop             = 2;  // 1 = genset stopped (read)
  constexpr uint16_t kCoilCoolDown         = 3;  // 1 = cool-down active (read; extension)
  constexpr uint16_t kCoilAutomatic        = 4;  // 1 = automatic (F320 byte0 bit3)
  constexpr uint16_t kCoilManual           = 5;  // 1 = manual (F320 byte0 bit2)
  constexpr uint16_t kCoilLockedMode       = 6;  // CCMODBUS: lock mode (unmapped)
  constexpr uint16_t kCoilFuelTransferPump = 7;

  // Discrete / input status
  constexpr uint16_t kDiscreteFuelReserve  = 6;
  constexpr uint16_t kDiscreteLowFuelLevel = 18;

  // Input registers
  constexpr uint16_t kIrFuelLevel          = 26;  // ‰
  constexpr uint16_t kIrSpeedRpm           = 25;
  constexpr uint16_t kIrBattAltDv          = 27;  // battery charging alternator, dV
  constexpr uint16_t kIrBatteryDv          = 28;  // start battery voltage, dV
  constexpr uint16_t kIrCoolDownTimer      = 29;  // cool-down remaining seconds (extension)
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
