#pragma once

#include "registers.h"

bool modbusSlaveBegin(GensetRegisters& regs);
void modbusSlaveTask();
void modbusSlaveSyncFromRegisters(GensetRegisters& regs);
