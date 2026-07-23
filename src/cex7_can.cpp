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
  for (size_t i = 0; i < kCoolCandSlots; ++i) {
    state.coolCand[i].id = 0;
    state.coolCand[i].lastDlc = 0;
    state.coolCand[i].valid = false;
  }
  state.lastCoolDownPrintedValid = false;
  state.lastCoolDownPrinted = false;
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

// CoolDown start pulse on F320/FF20 (does not touch Start/Stop).
// byte2 bit4 clear while byte1==2C is a short pulse (cool-down timer start),
// not the whole cool-down interval — caller must latch until Stop.
// Returns true when this frame should update CoolDown (set pulse or clear on Stop).
// Returns false while Start=1 but pulse ended — keep existing latched CoolDown.
bool cex7CanTryDecodeCoolDown(const CanFrame& frame, bool& coolDownOut) {
  static constexpr uint32_t kPhaseCanIdA = 0x0201F320;
  static constexpr uint32_t kPhaseCanIdB = 0x0201FF20;
  static constexpr uint8_t kStoppedBitMask = 0x40;
  static constexpr uint8_t kCoolModeByte1 = 0x2C;
  static constexpr uint8_t kCoolTimerStartBit = 0x10;  // byte2: clear = timer start pulse

  if (!frame.extended || (frame.id != kPhaseCanIdA && frame.id != kPhaseCanIdB)) {
    return false;
  }
  if (frame.dlc < 3) {
    return false;
  }
  const bool operating = (frame.data[0] & kStoppedBitMask) == 0;
  if (!operating) {
    coolDownOut = false;  // Stop ends cool-down latch
    return true;
  }
  const bool startPulse =
      (frame.data[1] == kCoolModeByte1) &&
      ((frame.data[2] & kCoolTimerStartBit) == 0);
  if (startPulse) {
    coolDownOut = true;  // arm / refresh latch
    return true;
  }
  return false;  // still Start=1, pulse over — keep latched value
}

// Cool-down remaining seconds: 0x0201FF14 byte4 counts down (e.g. B3→00 ≈ 179 s).
// Example: [33 02 08 00 3D 00 00 00] → [33 02 08 00 3C 00 00 00]
bool cex7CanTryDecodeCoolDownTimer(const CanFrame& frame, uint8_t& timerOut) {
  static constexpr uint32_t kTimerCanId = 0x0201FF14;
  static constexpr uint8_t kTimerByteIndex = 4;

  if (!frame.extended || frame.id != kTimerCanId) {
    return false;
  }
  if (frame.dlc <= kTimerByteIndex) {
    return false;
  }
  timerOut = frame.data[kTimerByteIndex];
  return true;
}

#if COOLDOWN_CAND_WATCH
// Focus IDs while CoolDown latched — find sustained cool-down signal.
static bool isCoolCandId(uint32_t id) {
  return id == 0x00010600u || id == 0x0201F320u || id == 0x0201FF20u ||
         id == 0x0201FF14u || id == 0x0201FF00u || id == 0x0201FF24u;
}

static void coolCandWatch(Cex7CanState& state, const CanFrame& frame,
                          const GensetRegisters& regs) {
  // Only log changes while CoolDown is latched (or on the Stop edge)
  static bool wasCoolOrStop = false;
  const bool coolOrStop = regs.coolDown || regs.coils[RegPdu::kCoilStop];
  if (!coolOrStop && !wasCoolOrStop) {
    // Still seed first samples of focus IDs so first cool change has a "was"
    if (!frame.extended || !isCoolCandId(frame.id)) {
      return;
    }
  }
  wasCoolOrStop = coolOrStop;

  if (!frame.extended || !isCoolCandId(frame.id)) {
    return;
  }
  // Skip spam unless CoolDown latched or Stop (capture cool→stop window)
  if (!regs.coolDown && !regs.coils[RegPdu::kCoilStop]) {
    // Update slot silently so first CoolDown change has baseline
    CoolCandSlot* slot = nullptr;
    for (size_t i = 0; i < kCoolCandSlots; ++i) {
      if (state.coolCand[i].valid && state.coolCand[i].id == frame.id) {
        slot = &state.coolCand[i];
        break;
      }
    }
    if (slot == nullptr) {
      for (size_t i = 0; i < kCoolCandSlots; ++i) {
        if (!state.coolCand[i].valid) {
          slot = &state.coolCand[i];
          slot->id = frame.id;
          slot->valid = true;
          break;
        }
      }
    }
    if (slot != nullptr) {
      const uint8_t n = frame.dlc > 8 ? 8 : frame.dlc;
      memcpy(slot->lastData, frame.data, n);
      slot->lastDlc = n;
    }
    return;
  }

  CoolCandSlot* slot = nullptr;
  for (size_t i = 0; i < kCoolCandSlots; ++i) {
    if (state.coolCand[i].valid && state.coolCand[i].id == frame.id) {
      slot = &state.coolCand[i];
      break;
    }
  }
  if (slot == nullptr) {
    for (size_t i = 0; i < kCoolCandSlots; ++i) {
      if (!state.coolCand[i].valid) {
        slot = &state.coolCand[i];
        slot->id = frame.id;
        slot->valid = true;
        slot->lastDlc = 0;
        break;
      }
    }
  }
  if (slot == nullptr) {
    return;
  }

  const uint8_t n = frame.dlc > 8 ? 8 : frame.dlc;
  const bool changed =
      (slot->lastDlc != n) || (memcmp(slot->lastData, frame.data, n) != 0);
  if (!changed) {
    return;
  }

  Serial.printf("[CAND] 0x%08lX ", static_cast<unsigned long>(frame.id));
  if (slot->lastDlc > 0) {
    for (uint8_t i = 0; i < slot->lastDlc; ++i) {
      Serial.printf("%02X", slot->lastData[i]);
      if (i + 1 < slot->lastDlc) {
        Serial.print(' ');
      }
    }
    Serial.print(F(" → "));
  }
  for (uint8_t i = 0; i < n; ++i) {
    Serial.printf("%02X", frame.data[i]);
    if (i + 1 < n) {
      Serial.print(' ');
    }
  }
  if (frame.id == 0x0201F320u || frame.id == 0x0201FF20u) {
    Serial.printf("  bit6=%u byte1=%02X byte2=%02X",
                  (frame.data[0] >> 6) & 1u,
                  (n > 1) ? frame.data[1] : 0,
                  (n > 2) ? frame.data[2] : 0);
  }
  Serial.printf("  | Start=%d Stop=%d CoolDown=%d\n",
                regs.coils[RegPdu::kCoilStartUp] ? 1 : 0,
                regs.coils[RegPdu::kCoilStop] ? 1 : 0,
                regs.coolDown ? 1 : 0);

  memcpy(slot->lastData, frame.data, n);
  slot->lastDlc = n;
}
#endif

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
#if RUN_STATE_HUNT_ENABLE || FUEL_HUNT_ENABLE
  pushRing(state, frame);
  trackId(state, frame);
  state.framesSincePrint++;
  regs.canRxCount = canTwaiRxCount();
  regs.canUniqueIds = state.uniqueIdCount;
#endif

  // Known IDs for decode (Start/Stop still from F320 bit6 — do not alter)
  static constexpr uint32_t kStatusCanId = 0x0201FF05;
  static constexpr uint32_t kHoursCanId = 0x0201FF13;
  static constexpr uint32_t kPhaseCanIdA = 0x0201F320;
  static constexpr uint32_t kPhaseCanIdB = 0x0201FF20;
  static constexpr uint32_t kTimerCanId = 0x0201FF14;

  const bool decodeId = frame.extended &&
                        (frame.id == kStatusCanId || frame.id == kHoursCanId ||
                         frame.id == kPhaseCanIdA || frame.id == kPhaseCanIdB ||
                         frame.id == kTimerCanId);

  if (decodeId) {
#if !(RUN_STATE_HUNT_ENABLE || FUEL_HUNT_ENABLE)
    state.framesSincePrint++;
    regs.canRxCount = canTwaiRxCount();
    regs.canUniqueIds = state.uniqueIdCount;
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
      registersSetRunState(regs, operating);  // coils 1/2 from F320 bit6 only
    }

#if COOLDOWN_MAP_ENABLE
    bool coolDown = false;
    if (cex7CanTryDecodeCoolDown(frame, coolDown)) {
      const bool prev = regs.coolDown;
      registersSetCoolDown(regs, coolDown);
      if (!state.lastCoolDownPrintedValid || coolDown != prev) {
        Serial.printf("[NOTE] CoolDown %d→%d  (Start=%d Stop=%d  latch until Stop)\n",
                      prev ? 1 : 0, coolDown ? 1 : 0,
                      regs.coils[RegPdu::kCoilStartUp] ? 1 : 0,
                      regs.coils[RegPdu::kCoilStop] ? 1 : 0);
        state.lastCoolDownPrinted = coolDown;
        state.lastCoolDownPrintedValid = true;
      }
    }
#else
    registersSetCoolDown(regs, false);
#endif

    // Always decode FF14 timer for Modbus IR 29 (Serial notes only if enabled)
    uint8_t coolTimer = 0;
    if (cex7CanTryDecodeCoolDownTimer(frame, coolTimer)) {
#if COOLDOWN_TIMER_ENABLE
      const uint8_t prevT = regs.coolDownTimer;
      const bool hadT = regs.coolDownTimerValid;
#endif
      registersSetCoolDownTimer(regs, coolTimer);
#if COOLDOWN_TIMER_ENABLE
      if (!hadT || (prevT == 0 && coolTimer != 0) ||
          (regs.coolDown && coolTimer == 0 && prevT != 0)) {
        Serial.printf("[NOTE] CoolDownTimer=%u (0x%02X)  CoolDown=%d\n",
                      coolTimer, coolTimer, regs.coolDown ? 1 : 0);
      }
#endif
    }
  }

#if COOLDOWN_CAND_WATCH
  coolCandWatch(state, frame, regs);
#endif
}

void cex7CanPrintStats(Cex7CanState& state, const GensetRegisters& regs) {
  const uint32_t now = millis();
  if (now - state.lastPrintMs < kStatusPrintMs) {
    return;
  }

  const uint32_t elapsed = now - state.lastPrintMs;
  const float fps = elapsed > 0 ? (state.framesSincePrint * 1000.0f) / elapsed : 0.0f;

  Serial.println(F("---- status ----"));
  Serial.printf("CAN rx=%lu fps=%.1f\n",
                static_cast<unsigned long>(canTwaiRxCount()), fps);
  Serial.printf("Fuel=%u‰  Batt=%.1fV  RPM=%u  Hours=%uh\n",
                regs.inputRegs[RegPdu::kIrFuelLevel],
                regs.inputRegs[RegPdu::kIrBatteryDv] / 10.0f,
                regs.inputRegs[RegPdu::kIrSpeedRpm],
                regs.inputRegs[RegPdu::kIrEngineHoursHh]);
  Serial.printf("Start=%d  Stop=%d  CoolDown=%d",
                regs.coils[RegPdu::kCoilStartUp] ? 1 : 0,
                regs.coils[RegPdu::kCoilStop] ? 1 : 0,
                regs.coolDown ? 1 : 0);
#if COOLDOWN_TIMER_ENABLE
  if (regs.coolDownTimerValid) {
    Serial.printf("  CoolDownTimer=%u", regs.coolDownTimer);
  } else {
    Serial.print(F("  CoolDownTimer=?"));
  }
#endif
  Serial.println();

#if RUN_STATE_HUNT_ENABLE
  Serial.println(F("COOL-DOWN HUNT: Start=1, press Stop on panel, paste RUNNING/COOLING/STOPPED blocks"));

  // Sort all tracked IDs
  size_t order[kMaxTrackedIds];
  size_t nUsed = 0;
  for (size_t i = 0; i < kMaxTrackedIds; ++i) {
    if (state.idStats[i].used) {
      order[nUsed++] = i;
    }
  }
  for (size_t a = 0; a + 1 < nUsed; ++a) {
    for (size_t b = a + 1; b < nUsed; ++b) {
      if (state.idStats[order[b]].id < state.idStats[order[a]].id) {
        const size_t tmp = order[a];
        order[a] = order[b];
        order[b] = tmp;
      }
    }
  }

  Serial.printf("CAN IDs ALL (%u):\n", static_cast<unsigned>(nUsed));
  for (size_t k = 0; k < nUsed; ++k) {
    const CanIdStat& st = state.idStats[order[k]];
    Serial.printf("  0x%08lX [", static_cast<unsigned long>(st.id));
    for (uint8_t b = 0; b < st.lastDlc; ++b) {
      Serial.printf("%02X", st.lastData[b]);
      if (b + 1 < st.lastDlc) {
        Serial.print(' ');
      }
    }
    Serial.println(']');
  }

  Serial.println(F("CHANGED since last status (noisy clock/hours filtered):"));
  size_t changeCount = 0;
  for (size_t k = 0; k < nUsed; ++k) {
    CanIdStat& st = state.idStats[order[k]];
    if (!st.prevValid) {
      continue;
    }
    if (st.id == 0x0002F101u || st.id == 0x0201FF13u) {
      memcpy(st.prevData, st.lastData, 8);
      st.prevDlc = st.lastDlc;
      continue;
    }
    const uint8_t maxLen = st.lastDlc > st.prevDlc ? st.lastDlc : st.prevDlc;
    bool changed = (st.lastDlc != st.prevDlc);
    uint8_t xorBuf[8] = {};
    for (uint8_t i = 0; i < maxLen; ++i) {
      const uint8_t a = (i < st.prevDlc) ? st.prevData[i] : 0;
      const uint8_t b = (i < st.lastDlc) ? st.lastData[i] : 0;
      xorBuf[i] = static_cast<uint8_t>(a ^ b);
      if (xorBuf[i] != 0) {
        changed = true;
      }
    }
    if (!changed) {
      continue;
    }
    ++changeCount;
    Serial.printf("  CHANGED 0x%08lX was=[", static_cast<unsigned long>(st.id));
    for (uint8_t i = 0; i < st.prevDlc; ++i) {
      Serial.printf("%02X", st.prevData[i]);
      if (i + 1 < st.prevDlc) {
        Serial.print(' ');
      }
    }
    Serial.print(F("] now=["));
    for (uint8_t i = 0; i < st.lastDlc; ++i) {
      Serial.printf("%02X", st.lastData[i]);
      if (i + 1 < st.lastDlc) {
        Serial.print(' ');
      }
    }
    Serial.println(']');
  }
  if (changeCount == 0) {
    Serial.println(F("  (no non-ticker changes)"));
  }

  for (size_t i = 0; i < kMaxTrackedIds; ++i) {
    if (!state.idStats[i].used) {
      continue;
    }
    memcpy(state.idStats[i].prevData, state.idStats[i].lastData, 8);
    state.idStats[i].prevDlc = state.idStats[i].lastDlc;
    state.idStats[i].prevValid = true;
  }
#endif

  state.framesSincePrint = 0;
  state.lastPrintMs = now;
}
