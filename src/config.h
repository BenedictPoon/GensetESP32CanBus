#pragma once

#include <cstdint>

#include "board_pins.h"

// CCMODBUS defaults from HIMOINSA CCMODBUS_En.pdf
static constexpr uint8_t  kModbusSlaveId   = 0x57;  // 87 decimal
static constexpr uint32_t kModbusBaud      = 9600;
static constexpr uint32_t kCanBitrate      = 50000;  // CEx7 CAN: 50 kbit/s
static constexpr uint32_t kSerialBaud      = 115200;

// Serial status / sniffer period
static constexpr uint32_t kStatusPrintMs   = 2000;
static constexpr uint32_t kCanSniffPrintMs = 500;

// Set true to log every CAN frame to USB Serial (noisy). Stats always print.
#ifndef CAN_LOG_EVERY_FRAME
#define CAN_LOG_EVERY_FRAME 0
#endif

// Set true for listen-only CAN (safe while CCMODBUS is still on the bus).
#ifndef CAN_LISTEN_ONLY
#define CAN_LISTEN_ONLY 1
#endif

// Fuel reverse-engineering: hunt payloads that encode this tank % (e.g. 71).
#ifndef FUEL_HUNT_PERCENT
#define FUEL_HUNT_PERCENT 71
#endif

#ifndef FUEL_HUNT_ENABLE
#define FUEL_HUNT_ENABLE 0
#endif

// Diff-hunt for Start-up / Stop coil status (CCMODBUS coils PDU 1 / 2).
// Toggle genset run/stop and watch Serial for CHANGED IDs / bit flips.
#ifndef RUN_STATE_HUNT_ENABLE
#define RUN_STATE_HUNT_ENABLE 0
#endif

// CCMODBUS fuel input register (PDU address)
static constexpr uint16_t kFuelLevelPduAddr = 26;
