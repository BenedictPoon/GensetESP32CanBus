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
  pushRing(state, frame);
  trackId(state, frame);
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
}

void cex7CanPrintStats(Cex7CanState& state, const GensetRegisters& regs) {
  const uint32_t now = millis();
  if (now - state.lastPrintMs < kStatusPrintMs) {
    return;
  }

  const uint32_t elapsed = now - state.lastPrintMs;
  const float fps = elapsed > 0 ? (state.framesSincePrint * 1000.0f) / elapsed : 0.0f;

  Serial.println(F("---- status ----"));
  Serial.printf("CAN rx=%lu tx=%lu err=%lu unique_ids=%lu ~%.1f frame/s listen_only=%d\n",
                static_cast<unsigned long>(canTwaiRxCount()),
                static_cast<unsigned long>(canTwaiTxCount()),
                static_cast<unsigned long>(canTwaiErrorCount()),
                static_cast<unsigned long>(state.uniqueIdCount), fps,
                canTwaiIsListenOnly() ? 1 : 0);
  Serial.printf("Fuel IR[%u]=%u ‰ (%.1f%%) valid=%d mapping=%d hunt=%u%%\n",
                static_cast<unsigned>(RegPdu::kIrFuelLevel),
                regs.inputRegs[RegPdu::kIrFuelLevel], registersFuelPercent(regs),
                regs.fuelValid ? 1 : 0, state.mappingConfigured ? 1 : 0,
                static_cast<unsigned>(FUEL_HUNT_PERCENT));

  // Sort indices by CAN ID for stable dump of ALL IDs (not just first 16)
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

  Serial.println(F("CAN IDs seen ALL (sorted):"));
  for (size_t k = 0; k < nUsed; ++k) {
    const CanIdStat& st = state.idStats[order[k]];
    Serial.printf("  0x%08lX %s x%lu  [", static_cast<unsigned long>(st.id),
                  st.extended ? "EXT" : "STD", static_cast<unsigned long>(st.count));
    for (uint8_t b = 0; b < st.lastDlc; ++b) {
      Serial.printf("%02X", st.lastData[b]);
      if (b + 1 < st.lastDlc) {
        Serial.print(' ');
      }
    }
    Serial.println(']');
  }
  if (nUsed == 0) {
    Serial.println(F("  (none yet — check CAN wiring, 50 kbit/s, terminators)"));
  }

#if FUEL_HUNT_ENABLE
  Serial.printf("FUEL HUNT for %u%% (= %u ‰):\n",
                static_cast<unsigned>(FUEL_HUNT_PERCENT),
                static_cast<unsigned>(FUEL_HUNT_PERCENT) * 10);
  for (size_t k = 0; k < nUsed; ++k) {
    huntFuelInStat(state.idStats[order[k]], FUEL_HUNT_PERCENT);
  }
  Serial.println(F("(If no >>> lines: 71% not in last payloads — paste the full ALL IDs list)"));
#endif

  state.framesSincePrint = 0;
  state.lastPrintMs = now;
}
