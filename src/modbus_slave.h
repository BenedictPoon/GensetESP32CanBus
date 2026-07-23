#pragma once

#include "registers.h"

bool modbusSlaveBegin(GensetRegisters& regs);
void modbusSlaveTask();
// Full image sync (startup / rare)
void modbusSlaveSyncFromRegisters(GensetRegisters& regs);
// Fast path: only live decoded fields (coils + key input regs)
void modbusSlaveSyncLive(GensetRegisters& regs);
