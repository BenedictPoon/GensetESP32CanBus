# ESP32 CCMODBUS Replacement — Setup Guide

Simple, detailed steps to flash the Waveshare **ESP32-S3-RS485-CAN** and use it as a HIMOINSA **CCMODBUS** replacement for **CEx7** CAN (fuel first).

Reference: [CCMODBUS_En.pdf](../CCMODBUS_En.pdf)  
Board wiki: [Waveshare ESP32-S3-RS485-CAN](https://www.waveshare.com/wiki/ESP32-S3-RS485-CAN)

---

## What this firmware does

| Side | Role |
|------|------|
| **RS485** | Modbus RTU **slave**, ID `0x57` (87), **9600 8N1** — same defaults as CCMODBUS |
| **CAN** | TWAI at **50 kbit/s** toward the CEx7 bus (listen-only by default) |
| **Fuel** | Input Register PDU address **26**, unit **‰** (divide by 10 for %) |

**Important:** The CEx7 CAN message format is proprietary and is **not** in the CCMODBUS manual. Until you reverse-engineer the fuel frame (steps below), Modbus fuel reads return `0` and `valid=0` on the serial status line. The Modbus slave itself works immediately.

---

## 1. What you need

- Waveshare **ESP32-S3-RS485-CAN**
- USB-C cable (power + flash + serial)
- PC with [PlatformIO](https://platformio.org/) (recommended) or Arduino IDE
- Twisted pair for CAN (CANH / CANL) to the genset CEx7 CAN bus
- RS485 A+ / B− to your Modbus master (USB-RS485 adapter is fine for testing)
- Optional but recommended for mapping: the **existing CCMODBUS** still on the CAN bus

---

## 2. Install PlatformIO (recommended IDE)

1. Install [Visual Studio Code](https://code.visualstudio.com/) or use **Cursor**.
2. Install the **PlatformIO IDE** extension.
3. Open this folder: `GensetESP32CanBus` (the folder that contains `platformio.ini`).
4. Wait for PlatformIO to install the `espressif32` platform and libraries on first open.

**CLI alternative:**

```bash
# Install PlatformIO Core if needed, then:
cd /path/to/GensetESP32CanBus
pio run
pio run -t upload
pio device monitor
```

PlatformIO works well with this board. If something fails, use the Arduino IDE fallback at the end of this guide.

---

## 3. Build and flash

1. Connect the board with **USB-C only** (do not also power the 7–36 V screw terminal at the same time).
2. In PlatformIO: **Build**, then **Upload**.
3. If upload fails:
   - Hold **BOOT**, press **RESET**, release **RESET**, then release **BOOT** (download mode).
   - Retry upload.
4. Open **Serial Monitor** at **115200** baud.

You should see something like:

```text
ESP32 CCMODBUS Replacement (CEx7 CAN)
[CAN] TWAI started @ 50000 bit/s ... listen_only=1
[MODBUS] Slave ID=0x57 baud=9600 ...
Fuel IR[26]=0 ‰ (0.0%) valid=0 mapping=0
```

---

## 4. Wiring

### Power

| Mode | Connection |
|------|------------|
| Lab / flash | USB-C only |
| Field | Screw terminal **7–36 V DC** only (same range as genset battery is fine) |

**Never** use USB-C and the screw terminal together.

### CAN (to CEx7)

| ESP32 terminal | Genset / bus |
|----------------|--------------|
| **H** | CANH |
| **L** | CANL |

- Baud is fixed in firmware to **50 000 bit/s** (CCMODBUS / CEx7).
- End nodes need **120 Ω** between CANH and CANL. On this board, enable the onboard CAN **120R** jumper if this ESP32 is an end of the bus.
- If CCMODBUS (or another end node) already provides termination, do **not** enable a second terminator in the middle of the bus.

### RS485 (Modbus master)

| ESP32 terminal | Modbus master |
|----------------|---------------|
| **A+** | A / D+ |
| **B-** | B / D- |

- Defaults: slave ID **0x57**, **9600**, 8 data bits, no parity, 1 stop bit.
- Enable the onboard RS485 **120R** jumper if this board is an end of the RS485 segment.

### Pin map (internal — already set in firmware)

| Function | GPIO |
|----------|------|
| CAN TX | 15 |
| CAN RX | 16 |
| RS485 TX | 17 |
| RS485 RX | 18 |
| RS485 DE/RE | 21 |

---

## 5. Test Modbus fuel register (no CAN decode required)

Use any Modbus RTU master (QModMaster, Modbus Poll, pymodbus, PLC, etc.).

| Setting | Value |
|---------|-------|
| Slave ID | `87` or `0x57` |
| Mode | RTU |
| Baud | 9600 8N1 |
| Function | **04** Read Input Registers |
| Start address | **26** (PDU address) |
| Quantity | 1 |

**Example request** (same style as CCMODBUS manual):

```text
57 04 00 1A 00 01 CRC_LO CRC_HI
```

- Address `0x001A` = PDU **26** (fuel level).
- Response data is fuel in **‰**. Example: `0x01F4` = 500 ‰ = **50.0%**.

Until CAN mapping is done, expect **0**.

Also readable:

| Item | Function | PDU addr |
|------|----------|----------|
| Fuel reserve alarm | FC02 | 6 |
| Low fuel level alarm | FC02 | 18 |
| Fuel transfer pump coil | FC01 | 7 |
| Fuel pump min/max thresholds | FC03 | 31 / 32 |

---

## 6. Confirmed CAN → Modbus mapping

| Signal | CAN ID | Decode | Modbus | Unit |
|--------|--------|--------|--------|------|
| Fuel | `0x0201FF05` | u16 LE @ bytes 0–1 | FC04 PDU **26** | ‰ (÷10 → %) |
| Start battery | `0x0201FF05` | u8 @ byte 4 | FC04 PDU **28** | dV (÷10 → V) |
| Engine RPM | `0x0201FF05` | u16 LE @ bytes 6–7 | FC04 PDU **25** | rpm (telemetry only) |
| Start-up / Stop | `0x0201F320` / `0x0201FF20` | byte0 **bit6** (`0x40`): set=stopped | FC01 coil **1** / **2** | Start=`!bit6`, Stop=`bit6` |
| CoolDown | `0x0201F320` / `0x0201FF20` | latch: byte1=`2C` + byte2 bit4 clear → until Stop | FC01 coil **3** | 1 while panel Cool Down (extension; with Start/Stop) |
| Automatic / Manual | `0x0201F320` / `0x0201FF20` | byte0 **bit3** Auto / **bit2** Manual | FC01 coil **4** / **5** | **confirmed** bits; PDU 4/5 so CoolDown stays on 3 |
| CoolDownTimer | `0x0201FF14` | byte4 countdown (~1 s/step) | FC04 PDU **29** | seconds (0 if not cooling) |
| Engine hours | `0x0201FF13` | u24 LE minutes @ 0–2 | FC04 PDU **41** / **42** | hh + mm:ss |

Examples from capture:
- Fuel `C5 02` → 709 ‰ ≈ 70.9%
- Battery stopped `7D` → 12.5 V; running `90` → 14.4 V (alternator charging)
- RPM stopped `00 00` → 0; running `36 06` → **1590** rpm (IR25 only)
- Start/Stop: F320 byte0 `CB` (bit6=1) → Stop; `8B` (bit6=0) → Start
- Auto/Manual (**confirmed**): F320 byte0 `CB` → Auto=1 Manual=0; `C7` → Auto=0 Manual=1 (`xor=0x0C` on bit2/bit3)
- Hours `BC A3 04` → 304060 min ≈ 5067 h + 40 min

Corroborating (not used for coils):
- `0x00010600` `[00 00]` stopped → `[A0 ..]` running

### Older reverse-engineering notes

If you need to re-hunt another signal, set `-DFUEL_HUNT_ENABLE=1` in `platformio.ini` and use the workflow below.

### Start / Stop / CoolDown / Auto / Manual coils (**confirmed** reads)

Reads (not commands):
- Coil **1** Start-up → `1` if operating (`F320` byte0 **bit6** clear)
- Coil **2** Stop → `1` if stopped (`F320` byte0 **bit6** set)
- Coil **3** CoolDown → `1` while cool-down active (**extension**; latch unchanged)
- Coil **4** Automatic → `1` if Auto (`F320` byte0 **bit3**) — **user-confirmed**
- Coil **5** Manual → `1` if Manual (`F320` byte0 **bit2**) — **user-confirmed**

Node-RED: FC **1**, address **1**, quantity **5** → `[Start, Stop, CoolDown, Automatic, Manual]`.  
(Qty **3** still = Start/Stop/CoolDown only.) Optional timer: FC **4** address **29**.

Start/Stop/CoolDown decode is unchanged; Auto/Manual are additional. FC05 writes for mode exist in CCMODBUS but are **not** forwarded to CAN yet (`CAN_LISTEN_ONLY`).

RPM (IR25) is independent telemetry and does **not** drive these coils.

1. Flash this firmware with **listen-only CAN** (default: `CAN_LISTEN_ONLY=1` in `platformio.ini` `build_flags`).
2. Leave the **real CCMODBUS** connected to the CEx7 CAN bus.
3. Tap the ESP32 onto the **same CANH/CANL** (passive listener). Mind terminators (only ends of the bus).
4. Open Serial Monitor. Every ~2 s you get:
   - Frame rate and **unique CAN IDs**
   - Last payload bytes per ID
5. Optional: set `-DCAN_LOG_EVERY_FRAME=1` in `platformio.ini` `build_flags`, rebuild, and capture a full log.
6. On a PC, poll **CCMODBUS** Modbus fuel (IR 26) while watching the ESP32 CAN log.
7. Change fuel (or wait for a slow change). Find which CAN ID / byte(s) move with IR 26.
8. Edit `cex7CanInit()` in `src/cex7_can.cpp`:

```cpp
state.mappingConfigured = true;
state.fuelCanId = 0xYOUR_ID;   // from capture
state.fuelExtended = false;    // or true
state.fuelByteIndex = 0;       // start byte
state.fuelByteCount = 2;       // 1 or 2
state.fuelLittleEndian = true;
state.fuelScaleNum = 1;        // adjust so raw → ‰
state.fuelScaleDen = 1;
```

9. Rebuild and flash. Confirm Serial shows `valid=1` and Modbus IR 26 matches CCMODBUS.
10. Power down, **remove CCMODBUS**, leave the ESP32 as the only Modbus gateway.
11. Optionally set `-DCAN_LISTEN_ONLY=0` in `platformio.ini` when you later add CAN transmit (commands). Keep listen-only until TX format is known.

---

## 7. Project files (quick map)

```text
platformio.ini          PlatformIO env for ESP32-S3
src/main.cpp            Setup / loop
src/config.h            Slave ID, baud, CAN bitrate, listen-only
src/board_pins.h        Waveshare GPIO map
src/registers.*         CCMODBUS register image (fuel IR 26)
src/modbus_slave.*      RS485 Modbus RTU slave
src/can_twai.*          TWAI @ 50 kbit/s
src/cex7_can.*          Sniffer + fuel decoder stub
docs/SETUP.md           This guide
CCMODBUS_En.pdf         Official register reference
```

---

## 8. Arduino IDE fallback

Use this only if PlatformIO is unavailable.

1. Install [Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. Boards Manager → install **esp32 by Espressif Systems** version **3.0.x**.
3. Board: **ESP32S3 Dev Module**.
4. Enable **USB CDC On Boot**.
5. Install libraries: **modbus-esp8266** (emelianov) via Library Manager.
6. Create a sketch and copy all `.cpp` / `.h` from `src/` into the sketch folder (or use PlatformIO export).
7. Select the correct COM port → Upload.
8. Same BOOT+RESET recovery if upload fails.

Waveshare’s official demos also target Arduino IDE; PlatformIO remains the easier workflow for this project.

---

## 9. Checklist

- [ ] Firmware builds and uploads
- [ ] Serial shows CAN + Modbus init lines
- [ ] Modbus master can read slave `0x57`, FC04, address 26
- [ ] With CEx7 connected, Serial shows rising `CAN rx=` and unique IDs
- [ ] After mapping, `Fuel IR[26]` tracks real level and Modbus matches

---

## 10. Safety notes

- Do not command genset start/stop from Modbus coils until CAN **transmit** mapping is verified — writes are accepted locally but are **not** forwarded to CEx7 yet.
- Work on the CAN bus with the genset in a safe state; listen-only mode avoids injecting frames during sniffing.
