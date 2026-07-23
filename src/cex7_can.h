#pragma once

#include "can_twai.h"
#include "registers.h"

static constexpr size_t kCanRingSize = 64;
static constexpr size_t kMaxTrackedIds = 64;
static constexpr size_t kCoolCandSlots = 10;

struct CanIdStat {
  uint32_t id;
  uint32_t count;
  uint8_t lastData[8];
  uint8_t lastDlc;
  uint8_t prevData[8];   // snapshot at last status print (run-state hunt)
  uint8_t prevDlc;
  bool prevValid;
  bool extended;
  bool used;
};

struct CoolCandSlot {
  uint32_t id;
  uint8_t lastData[8];
  uint8_t lastDlc;
  bool valid;
};

struct Cex7CanState {
  CanFrame ring[kCanRingSize];
  size_t ringHead;
  size_t ringCount;
  CanIdStat idStats[kMaxTrackedIds];
  uint32_t uniqueIdCount;
  uint32_t framesSincePrint;
  uint32_t lastPrintMs;
  bool mappingConfigured;
  // After reverse-engineering, set these and mappingConfigured=true
  uint32_t fuelCanId;
  bool fuelExtended;
  uint8_t fuelByteIndex;   // start byte of fuel value in payload
  uint8_t fuelByteCount;   // 1 or 2
  bool fuelLittleEndian;
  uint16_t fuelScaleNum;   // raw * num / den → ‰
  uint16_t fuelScaleDen;
  CoolCandSlot coolCand[kCoolCandSlots];
  bool lastCoolDownPrintedValid;
  bool lastCoolDownPrinted;
};

void cex7CanInit(Cex7CanState& state);
void cex7CanOnFrame(Cex7CanState& state, const CanFrame& frame, GensetRegisters& regs);
void cex7CanPrintStats(Cex7CanState& state, const GensetRegisters& regs);
bool cex7CanTryDecodeFuel(const Cex7CanState& state, const CanFrame& frame, uint16_t& permilleOut);
bool cex7CanTryDecodeBatteryDv(const CanFrame& frame, uint16_t& deciVoltsOut);
bool cex7CanTryDecodeEngineHoursMinutes(const CanFrame& frame, uint32_t& totalMinutesOut);
bool cex7CanTryDecodeEngineRpm(const CanFrame& frame, uint16_t& rpmOut);
// Start/Stop from 0x0201F320 / 0x0201FF20 byte0 bit6 (0x40): set=stopped
bool cex7CanTryDecodeRunState(const CanFrame& frame, bool& operatingOut);
// CoolDown: latch on F320 timer-start pulse until Stop (does not touch Start/Stop)
bool cex7CanTryDecodeCoolDown(const CanFrame& frame, bool& coolDownOut);
// Cool-down countdown seconds from 0x0201FF14 byte4 (optional)
bool cex7CanTryDecodeCoolDownTimer(const CanFrame& frame, uint8_t& timerOut);
