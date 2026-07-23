#include "modbus_slave.h"

#include <ModbusRTU.h>

#include "board_pins.h"
#include "config.h"

namespace {

ModbusRTU mb;
GensetRegisters* gRegs = nullptr;
HardwareSerial RS485(1);

// Callback: allow reads always; writes update local image but do not yet
// forward commands to CEx7 (CAN TX mapping unknown).
uint16_t onCoilWrite(TRegister* reg, uint16_t val) {
  if (gRegs == nullptr || reg == nullptr) {
    return val;
  }
  const uint16_t addr = reg->address.address;
  if (addr < kCoilCount) {
    gRegs->coils[addr] = (val != 0);
  }
  return val;
}

uint16_t onHoldingWrite(TRegister* reg, uint16_t val) {
  if (gRegs == nullptr || reg == nullptr) {
    return val;
  }
  const uint16_t addr = reg->address.address;
  if (addr < kHoldingCount) {
    gRegs->holding[addr] = val;
  }
  return val;
}

}  // namespace

bool modbusSlaveBegin(GensetRegisters& regs) {
  gRegs = &regs;

  pinMode(PIN_RS485_DE, OUTPUT);
  digitalWrite(PIN_RS485_DE, LOW);

  RS485.begin(kModbusBaud, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
  mb.begin(&RS485, PIN_RS485_DE);
  mb.slave(kModbusSlaveId);

  // Coils (FC01 / FC05)
  mb.addCoil(0, false, kCoilCount);
  mb.onSetCoil(0, onCoilWrite, kCoilCount);

  // Discrete inputs / input status (FC02)
  mb.addIsts(0, false, kDiscreteCount);

  // Holding registers (FC03 / FC06 / FC16)
  mb.addHreg(0, 0, kHoldingCount);
  for (uint16_t i = 0; i < kHoldingCount; ++i) {
    mb.Hreg(i, regs.holding[i]);
  }
  mb.onSetHreg(0, onHoldingWrite, kHoldingCount);

  // Input registers (FC04) — fuel at PDU 26
  mb.addIreg(0, 0, kInputRegisterCount);
  for (uint16_t i = 0; i < kInputRegisterCount; ++i) {
    mb.Ireg(i, regs.inputRegs[i]);
  }

  // J1939 block (optional; present when CCJ1939 / CEM7J traffic exists)
  mb.addIreg(kJ1939RegisterBase, 0, kJ1939RegisterCount);
  for (uint16_t i = 0; i < kJ1939RegisterCount; ++i) {
    mb.Ireg(kJ1939RegisterBase + i, regs.j1939[i]);
  }

  Serial.printf("[MODBUS] Slave ID=0x%02X baud=%lu RS485 TX=%d RX=%d DE=%d\n",
                kModbusSlaveId, static_cast<unsigned long>(kModbusBaud),
                PIN_RS485_TX, PIN_RS485_RX, PIN_RS485_DE);
  Serial.printf("[MODBUS] Fuel input register PDU addr=%u (‰)\n",
                static_cast<unsigned>(kFuelLevelPduAddr));
  return true;
}

void modbusSlaveSyncFromRegisters(GensetRegisters& regs) {
  for (uint16_t i = 0; i < kCoilCount; ++i) {
    mb.Coil(i, regs.coils[i]);
  }
  for (uint16_t i = 0; i < kDiscreteCount; ++i) {
    mb.Ists(i, regs.discrete[i]);
  }
  for (uint16_t i = 0; i < kInputRegisterCount; ++i) {
    mb.Ireg(i, regs.inputRegs[i]);
  }
  for (uint16_t i = 0; i < kHoldingCount; ++i) {
    // Prefer live image for non-settings; settings may be written by master
    if (i < RegPdu::kHrModbusId || i > RegPdu::kHrParity) {
      mb.Hreg(i, regs.holding[i]);
    } else {
      regs.holding[i] = mb.Hreg(i);
    }
  }
  for (uint16_t i = 0; i < kJ1939RegisterCount; ++i) {
    mb.Ireg(kJ1939RegisterBase + i, regs.j1939[i]);
  }
}

void modbusSlaveSyncLive(GensetRegisters& regs) {
  // Coils used for run state (and keep full coil bank small/cheap)
  mb.Coil(RegPdu::kCoilStartUp, regs.coils[RegPdu::kCoilStartUp]);
  mb.Coil(RegPdu::kCoilStop, regs.coils[RegPdu::kCoilStop]);
  mb.Coil(RegPdu::kCoilCoolDown, regs.coils[RegPdu::kCoilCoolDown]);
  mb.Coil(RegPdu::kCoilFuelTransferPump, regs.coils[RegPdu::kCoilFuelTransferPump]);

  mb.Ists(RegPdu::kDiscreteFuelReserve, regs.discrete[RegPdu::kDiscreteFuelReserve]);
  mb.Ists(RegPdu::kDiscreteLowFuelLevel, regs.discrete[RegPdu::kDiscreteLowFuelLevel]);

  mb.Ireg(RegPdu::kIrSpeedRpm, regs.inputRegs[RegPdu::kIrSpeedRpm]);
  mb.Ireg(RegPdu::kIrFuelLevel, regs.inputRegs[RegPdu::kIrFuelLevel]);
  mb.Ireg(RegPdu::kIrBatteryDv, regs.inputRegs[RegPdu::kIrBatteryDv]);
  mb.Ireg(RegPdu::kIrCoolDownTimer, regs.inputRegs[RegPdu::kIrCoolDownTimer]);
  mb.Ireg(RegPdu::kIrSensorPresence, regs.inputRegs[RegPdu::kIrSensorPresence]);
  mb.Ireg(RegPdu::kIrEngineHoursHh, regs.inputRegs[RegPdu::kIrEngineHoursHh]);
  mb.Ireg(RegPdu::kIrEngineHoursMmSs, regs.inputRegs[RegPdu::kIrEngineHoursMmSs]);
}

void modbusSlaveTask() {
  mb.task();
}
