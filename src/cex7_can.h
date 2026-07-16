#pragma once

#include "can_twai.h"
#include "registers.h"

static constexpr size_t kCanRingSize = 64;
static constexpr size_t kMaxTrackedIds = 64;

struct CanIdStat {
  uint32_t id;
  uint32_t count;
  uint8_t lastData[8];
  uint8_t lastDlc;
  bool extended;
  bool used;
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
};

void cex7CanInit(Cex7CanState& state);
void cex7CanOnFrame(Cex7CanState& state, const CanFrame& frame, GensetRegisters& regs);
void cex7CanPrintStats(Cex7CanState& state, const GensetRegisters& regs);
bool cex7CanTryDecodeFuel(const Cex7CanState& state, const CanFrame& frame, uint16_t& permilleOut);
