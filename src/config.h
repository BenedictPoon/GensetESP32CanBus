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

// Quiet cand watch during cool-down discovery (noisy). Keep 0 once mapped.
#ifndef COOLDOWN_CAND_WATCH
#define COOLDOWN_CAND_WATCH 0
#endif

// CoolDown latch on F320 timer-start pulse until Stop. Does not change Start/Stop.
#ifndef COOLDOWN_MAP_ENABLE
#define COOLDOWN_MAP_ENABLE 1
#endif

// Optional CoolDownTimer from 0x0201FF14 byte4 countdown (set 1 to enable).
#ifndef COOLDOWN_TIMER_ENABLE
#define COOLDOWN_TIMER_ENABLE 0
#endif

// Full-bus sniffer + payload diffs. Keep 0 for slim status view.
#ifndef RUN_STATE_HUNT_ENABLE
#define RUN_STATE_HUNT_ENABLE 0
#endif

// CCMODBUS fuel input register (PDU address)
static constexpr uint16_t kFuelLevelPduAddr = 26;
