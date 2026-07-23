#include "cex7_can.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"

void cex7CanInit(Cex7CanState& state) {
  memset(&state, 0, sizeof(state));

  // Mapped from live capture: panel ~71% matched CAN 0x0201FF05 bytes [C5 02]=709 ‰
  // CCMODBUS exposes the same value on Modbus Input Register PDU 26.
  state.mappingConfigured = true;
  state.fuelCanId = 0x0201FF05;
  state.fuelExtended = true;
  state.fuelByteIndex = 0;
  state.fuelByteCount = 2;
  state.fuelLittleEndian = true;
  state.fuelScaleNum = 1;
  state.fuelScaleDen = 1;
  state.lastPrintMs = millis();
}

static void trackId(Cex7CanState& state, const CanFrame& frame) {
  for (size_t i = 0; i < kMaxTrackedIds; ++i) {
    if (state.idStats[i].used && state.idStats[i].id == frame.id) {
      state.idStats[i].count++;
      state.idStats[i].lastDlc = frame.dlc;
      state.idStats[i].extended = frame.extended;
      memset(state.idStats[i].lastData, 0, 8);
      memcpy(state.idStats[i].lastData, frame.data, frame.dlc);
      return;
    }
  }
  for (size_t i = 0; i < kMaxTrackedIds; ++i) {
    if (!state.idStats[i].used) {
      state.idStats[i].used = true;
      state.idStats[i].id = frame.id;
      state.idStats[i].count = 1;
      state.idStats[i].lastDlc = frame.dlc;
      state.idStats[i].extended = frame.extended;
      memset(state.idStats[i].lastData, 0, 8);
      memcpy(state.idStats[i].lastData, frame.data, frame.dlc);
      state.uniqueIdCount++;
      return;
    }
  }
}

static void pushRing(Cex7CanState& state, const CanFrame& frame) {
  state.ring[state.ringHead] = frame;
  state.ringHead = (state.ringHead + 1) % kCanRingSize;
  if (state.ringCount < kCanRingSize) {
    state.ringCount++;
  }
}

bool cex7CanTryDecodeFuel(const Cex7CanState& state, const CanFrame& frame,
                          uint16_t& permilleOut) {
  if (!state.mappingConfigured) {
    return false;
  }
  if (frame.id != state.fuelCanId) {
    return false;
  }
  if (frame.extended != state.fuelExtended) {
    return false;
  }
  if (frame.dlc < state.fuelByteIndex + state.fuelByteCount) {
    return false;
  }

  uint32_t raw = 0;
  if (state.fuelByteCount == 1) {
    raw = frame.data[state.fuelByteIndex];
  } else {
    const uint8_t b0 = frame.data[state.fuelByteIndex];
    const uint8_t b1 = frame.data[state.fuelByteIndex + 1];
    raw = state.fuelLittleEndian ? (static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8))
                                 : (static_cast<uint32_t>(b1) | (static_cast<uint32_t>(b0) << 8));
  }

  if (state.fuelScaleDen == 0) {
    return false;
  }
  uint32_t permille = (raw * state.fuelScaleNum) / state.fuelScaleDen;
  if (permille > 1000) {
    permille = 1000;
  }
  permilleOut = static_cast<uint16_t>(permille);
  return true;
}

// Battery voltage (CCMODBUS IR 28, dV): same status frame as fuel, byte 4.
// Example: [C5 02 00 00 7B 00 00 00] → 0x7B = 123 → 12.3 V
bool cex7CanTryDecodeBatteryDv(const CanFrame& frame, uint16_t& deciVoltsOut) {
  static constexpr uint32_t kBatteryCanId = 0x0201FF05;
  static constexpr uint8_t kBatteryByteIndex = 4;

  if (!frame.extended || frame.id != kBatteryCanId) {
    return false;
  }
  if (frame.dlc <= kBatteryByteIndex) {
    return false;
  }
  deciVoltsOut = frame.data[kBatteryByteIndex];
  return true;
}

// Engine run hours (CCMODBUS IR 41 hh + IR 42 mm:ss): minutes as 24-bit LE.
// Example: [BC A3 04 1C ...] → 0x04A3BC = 304060 min → 5067 h + 40 min
bool cex7CanTryDecodeEngineHoursMinutes(const CanFrame& frame, uint32_t& totalMinutesOut) {
  static constexpr uint32_t kHoursCanId = 0x0201FF13;

  if (!frame.extended || frame.id != kHoursCanId) {
    return false;
  }
  if (frame.dlc < 3) {
    return false;
  }
  totalMinutesOut = static_cast<uint32_t>(frame.data[0]) |
                    (static_cast<uint32_t>(frame.data[1]) << 8) |
                    (static_cast<uint32_t>(frame.data[2]) << 16);
  return true;
}

// Engine RPM (CCMODBUS IR 25): same status frame as fuel/battery, bytes 6–7 LE.
// Telemetry only — does not drive Start/Stop coils.
bool cex7CanTryDecodeEngineRpm(const CanFrame& frame, uint16_t& rpmOut) {
  static constexpr uint32_t kRpmCanId = 0x0201FF05;
  static constexpr uint8_t kRpmByteIndex = 6;

  if (!frame.extended || frame.id != kRpmCanId) {
    return false;
  }
  if (frame.dlc < kRpmByteIndex + 2) {
    return false;
  }
  rpmOut = static_cast<uint16_t>(frame.data[kRpmByteIndex] |
                                 (static_cast<uint16_t>(frame.data[kRpmByteIndex + 1]) << 8));
  return true;
}

// Start/Stop from controller phase frame.
// 0x0201F320 / 0x0201FF20 byte0 bit6 (0x40): 1=stopped (e.g. CB), 0=operating (e.g. 8B).
bool cex7CanTryDecodeRunState(const CanFrame& frame, bool& operatingOut) {
  static constexpr uint32_t kPhaseCanIdA = 0x0201F320;
  static constexpr uint32_t kPhaseCanIdB = 0x0201FF20;
  static constexpr uint8_t kStoppedBitMask = 0x40;  // byte0 bit6

  if (!frame.extended || (frame.id != kPhaseCanIdA && frame.id != kPhaseCanIdB)) {
    return false;
  }
  if (frame.dlc < 1) {
    return false;
  }
  const bool stopped = (frame.data[0] & kStoppedBitMask) != 0;
  operatingOut = !stopped;
  return true;
}

#if FUEL_HUNT_ENABLE
// Scan one frame's payload for encodings of target tank %.
static void huntFuelInStat(const CanIdStat& st, uint16_t targetPct) {
  const uint16_t targetPm = static_cast<uint16_t>(targetPct) * 10;  // 71% → 710 ‰
  const uint8_t* d = st.lastData;
  const uint8_t n = st.lastDlc;
  if (n == 0) {
    return;
  }

  auto printHit = [&](const char* how, uint32_t raw, float pct) {
    Serial.printf("  >>> FUEL CANDIDATE ID=0x%08lX %s raw=%lu → %.1f%%  data=[",
                  static_cast<unsigned long>(st.id), how,
                  static_cast<unsigned long>(raw), pct);
    for (uint8_t i = 0; i < n; ++i) {
      Serial.printf("%02X", d[i]);
      if (i + 1 < n) {
        Serial.print(' ');
      }
    }
    Serial.println(']');
  };

  for (uint8_t i = 0; i < n; ++i) {
    const uint8_t b = d[i];
    char label[32];
    // Exact percent in one byte
    if (b == targetPct) {
      snprintf(label, sizeof(label), "u8%%@%u", i);
      printHit(label, b, static_cast<float>(b));
    }
    // CCMODBUS ‰ stored in one byte only if <=255 — rare for 710
    if (b == targetPm && targetPm <= 255) {
      snprintf(label, sizeof(label), "u8permille@%u", i);
      printHit(label, b, b / 10.0f);
    }
    // J1939 fuel level style: 0.4 %/bit
    const float j1939pct = b * 0.4f;
    if (j1939pct >= 0.1f && fabsf(j1939pct - static_cast<float>(targetPct)) <= 0.6f) {
      snprintf(label, sizeof(label), "u8x0.4%%@%u", i);
      printHit(label, b, j1939pct);
    }
  }

  for (uint8_t i = 0; i + 1 < n; ++i) {
    const uint16_t le = static_cast<uint16_t>(d[i] | (static_cast<uint16_t>(d[i + 1]) << 8));
    const uint16_t be = static_cast<uint16_t>((static_cast<uint16_t>(d[i]) << 8) | d[i + 1]);

    auto checkU16 = [&](const char* endian, uint16_t v) {
      if (v == targetPm) {
        char label[24];
        snprintf(label, sizeof(label), "u16%s‰@%u", endian, i);
        printHit(label, v, v / 10.0f);
      }
      if (v == targetPct) {
        char label[24];
        snprintf(label, sizeof(label), "u16%s%%@%u", endian, i);
        printHit(label, v, static_cast<float>(v));
      }
      // raw already in ‰-like 0..1000 close to target
      if (v > 0 && v <= 1000 && abs(static_cast<int>(v) - static_cast<int>(targetPm)) <= 5) {
        char label[28];
        snprintf(label, sizeof(label), "u16%s~‰@%u", endian, i);
        printHit(label, v, v / 10.0f);
      }
    };
    checkU16("LE", le);
    checkU16("BE", be);
  }

  // Highlight register-shaped IDs even with no numeric match
  const uint32_t low = st.id & 0xFFu;
  if (low == 0x1Au || low == 0x26u) {
    Serial.printf("  !! register-shaped ID=0x%08lX low=0x%02lX (fuel PDU is 26 / 0x1A) data=[",
                  static_cast<unsigned long>(st.id), static_cast<unsigned long>(low));
    for (uint8_t i = 0; i < n; ++i) {
      Serial.printf("%02X", d[i]);
      if (i + 1 < n) {
        Serial.print(' ');
      }
    }
    Serial.println(']');
  }
}
#endif

void cex7CanOnFrame(Cex7CanState& state, const CanFrame& frame, GensetRegisters& regs) {
  // Known IDs only: status (fuel/batt/rpm), hours, run-state phase
  static constexpr uint32_t kStatusCanId = 0x0201FF05;
  static constexpr uint32_t kHoursCanId = 0x0201FF13;
  static constexpr uint32_t kPhaseCanIdA = 0x0201F320;
  static constexpr uint32_t kPhaseCanIdB = 0x0201FF20;

  const bool known = frame.extended &&
                     (frame.id == kStatusCanId || frame.id == kHoursCanId ||
                      frame.id == kPhaseCanIdA || frame.id == kPhaseCanIdB);
  if (!known) {
    return;
  }

#if RUN_STATE_HUNT_ENABLE || FUEL_HUNT_ENABLE
  pushRing(state, frame);
  trackId(state, frame);
#endif
  state.framesSincePrint++;
  regs.canRxCount = canTwaiRxCount();
  regs.canUniqueIds = state.uniqueIdCount;

#if CAN_LOG_EVERY_FRAME
  Serial.printf("[CAN] %s ID=0x%08lX DLC=%u DATA=",
                frame.extended ? "EXT" : "STD",
                static_cast<unsigned long>(frame.id), frame.dlc);
  for (uint8_t i = 0; i < frame.dlc; ++i) {
    Serial.printf("%02X", frame.data[i]);
    if (i + 1 < frame.dlc) {
      Serial.print(' ');
    }
  }
  Serial.println();
#endif

  uint16_t fuel = 0;
  if (cex7CanTryDecodeFuel(state, frame, fuel)) {
    registersSetFuelPermille(regs, fuel);
  }

  uint16_t battDv = 0;
  if (cex7CanTryDecodeBatteryDv(frame, battDv)) {
    registersSetBatteryDv(regs, battDv);
  }

  uint32_t hoursMin = 0;
  if (cex7CanTryDecodeEngineHoursMinutes(frame, hoursMin)) {
    registersSetEngineHoursFromMinutes(regs, hoursMin);
  }

  uint16_t rpm = 0;
  if (cex7CanTryDecodeEngineRpm(frame, rpm)) {
    registersSetEngineRpm(regs, rpm);  // IR25 only — does not set Start/Stop
  }

  bool operating = false;
  if (cex7CanTryDecodeRunState(frame, operating)) {
    registersSetRunState(regs, operating);  // coils 1/2 from F320 bit6
  }
}

void cex7CanPrintStats(Cex7CanState& state, const GensetRegisters& regs) {
  const uint32_t now = millis();
  if (now - state.lastPrintMs < kStatusPrintMs) {
    return;
  }

  const uint32_t elapsed = now - state.lastPrintMs;
  const float fps = elapsed > 0 ? (state.framesSincePrint * 1000.0f) / elapsed : 0.0f;

  // Slim production status (no full ID dump / hunt)
  Serial.println(F("---- status ----"));
  Serial.printf("CAN rx=%lu tx=%lu err=%lu mapped_fps=%.1f listen_only=%d\n",
                static_cast<unsigned long>(canTwaiRxCount()),
                static_cast<unsigned long>(canTwaiTxCount()),
                static_cast<unsigned long>(canTwaiErrorCount()), fps,
                canTwaiIsListenOnly() ? 1 : 0);
  Serial.printf("Fuel IR[%u]=%u ‰ (%.1f%%)  Batt IR[%u]=%u dV (%.1f V)\n",
                static_cast<unsigned>(RegPdu::kIrFuelLevel),
                regs.inputRegs[RegPdu::kIrFuelLevel], registersFuelPercent(regs),
                static_cast<unsigned>(RegPdu::kIrBatteryDv),
                regs.inputRegs[RegPdu::kIrBatteryDv],
                regs.inputRegs[RegPdu::kIrBatteryDv] / 10.0f);
  Serial.printf("RPM  IR[%u]=%u\n",
                static_cast<unsigned>(RegPdu::kIrSpeedRpm),
                regs.inputRegs[RegPdu::kIrSpeedRpm]);
  Serial.printf("Start=%d Stop=%d  (from F320/FF20 byte0 bit6)\n",
                regs.coils[RegPdu::kCoilStartUp] ? 1 : 0,
                regs.coils[RegPdu::kCoilStop] ? 1 : 0);
  {
    const uint16_t hh = regs.inputRegs[RegPdu::kIrEngineHoursHh];
    const uint16_t mmSs = regs.inputRegs[RegPdu::kIrEngineHoursMmSs];
    const uint8_t mm = static_cast<uint8_t>((mmSs >> 8) & 0xFFu);
    const uint8_t ss = static_cast<uint8_t>(mmSs & 0xFFu);
    Serial.printf("Hours IR[%u]=%u h  IR[%u]=%02u:%02u\n",
                  static_cast<unsigned>(RegPdu::kIrEngineHoursHh), hh,
                  static_cast<unsigned>(RegPdu::kIrEngineHoursMmSs), mm, ss);
  }

  state.framesSincePrint = 0;
  state.lastPrintMs = now;
}
