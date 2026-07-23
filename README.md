# GensetESP32CanBus

ESP32-S3 firmware that listens to a HIMOINSA **CEx7** genset on CAN (50 kbit/s) and exposes a CCMODBUS-compatible Modbus RTU slave on RS485 (ID `0x57`, 9600 8N1).

Board: [Waveshare ESP32-S3-RS485-CAN](https://www.waveshare.com/wiki/ESP32-S3-RS485-CAN)  
Register map reference: `CCMODBUS_En.pdf`  
Setup steps: [docs/SETUP.md](docs/SETUP.md)

---

## Quick start

```bash
pio run -t upload && pio device monitor
```

Serial status (115200) shows fuel, battery, RPM, hours, `Start` / `Stop` / `CoolDown`.

---

## Reverse-engineered CEx7 CAN IDs

All IDs below are **extended** frames unless noted. Mapping was done from live bus captures (listen-only).

### Confirmed telemetry / status

| CAN ID | Role | Decode | Maps to |
|--------|------|--------|---------|
| `0x0201FF05` | Status: fuel, battery, RPM | Fuel u16 LE @ bytes 0–1 (‰); battery u8 @ byte 4 (dV); RPM u16 LE @ bytes 6–7 | Modbus IR **26** fuel, IR **28** batt, IR **25** rpm |
| `0x0201FF13` | Engine hours | u24 LE minutes @ bytes 0–2 | IR **41** hh, IR **42** mm:ss |
| `0x0201F320` / `0x0201FF20` | Phase / run state (same payload on both IDs) | **byte0 bit6 (`0x40`)**: Start/Stop; **bit3 (`0x08`)**: Automatic; **bit2 (`0x04`)**: Manual; CoolDown latch separate | Coils **1–5**: Start/Stop/CoolDown/Auto/Manual |

Start/Stop are **not** derived from RPM.

### Automatic / Manual mode (confirmed)

Same CAN IDs as Start/Stop: `0x0201F320` / `0x0201FF20`. Panel Auto↔Manual toggles **byte0 bits 2 and 3** only (`xor=0x0C`); bit6 (Start/Stop) is unchanged.

| Panel | F320 byte0 (stopped example) | bit3 (`0x08`) | bit2 (`0x04`) | Coil 4 Auto | Coil 5 Manual |
|-------|------------------------------|---------------|---------------|-------------|---------------|
| **Automatic** | `CB` | 1 | 0 | **1** | 0 |
| **Manual** | `C7` | 1→0 | 0→1 | 0 | **1** |

Live capture (user-confirmed): `CB ↔ C7` on button press; final state Auto = `CB`.

Firmware: `cex7CanTryDecodeControlMode()` → Modbus FC01 coils **4** / **5** (so CoolDown can stay on coil **3** with Start/Stop). Serial shows `Auto=` / `Manual=` on status and `[NOTE] Mode Auto …` on change.

### Cool-down

| CAN ID | Role | Decode | Notes |
|--------|------|--------|-------|
| `0x0201F320` / `0x0201FF20` | Cool-down **start pulse** | While Start=1: **byte1=`0x2C`** and **byte2 bit4 clear** (`10`→`00`) | Arms `CoolDown=1` (Modbus coil **3**). Latch until Stop. |
| `0x0201FF14` | Cool-down **countdown timer** | **byte4** counts down once per ~1 s (e.g. `B3`→`00` ≈ 179 s) | Example: `33 02 08 00 3D 00 00 00` → `… 3C …`. Also used as a short crank timer on start. Enable with `-DCOOLDOWN_TIMER_ENABLE=1`. |
| `0x00010600` | Mode / phase companion | Running often `A0 2A` or `A0 40`; at cool→stop hand-off → `04 42`; fully stopped → `00 00` | Correlates with F320 phase; not used for Start/Stop coils. |
| `0x0201FF24` | Correlated flag | Often `00 0E` ↔ `00 0A` with cool-down pulse / load changes | Supporting signal only. |

**CoolDown sequence (from terminal captures):**

1. Running: `Start=1 Stop=0 CoolDown=0`, F320 e.g. `8B 2C 10 …`
2. Stop pressed / cool-down starts: F320 `8B 2C 10 …` → `8B 2C 00 …`, FF24 `0E`→`0A` → **`CoolDown=1`** (latched)
3. During cool-down: Start stays **1**; FF14 byte4 counts down (`B3`…`3D`→`3C`…→`00`); F320 may move `2C`→`42`, `10600` `A0 2A`→`A0 40`
4. Cool-down ends → stopped: `10600` → `04 42`, F320 → `CB 40 …` (bit6=1) → **`Start=0 Stop=1 CoolDown=0`**, RPM coasts to 0

### Other IDs seen (not mapped to Modbus yet)

| CAN ID | Observed behaviour |
|--------|--------------------|
| `0x0002F101` | Clock / date ticker (noisy; filtered in hunt dumps) |
| `0x0201FF00` | Status-ish; often stable `E3 01 …` while running |
| `0x0201FF01` / `0x0201FF03` | Noisy analogs / counters while running |
| `0x0201F302` / `0x0201FF02` | Small oscillating counters (`02`↔`03`) |
| `0x0201FF06` / `0x0201FF07` / `0x0201FF0B` | Noisy while running |
| `0x0201FF11` / `0x0201FF12` | Related status / analogs |
| `0x02020100` / `0x0202FF00` | Present on bus; not decoded |
| `0x01010902` / `0x01010912` / `0x01010920` | Present; not decoded |
| `0x0201FF16` | Often `FF FF FF` |

---

## Build flags (`platformio.ini`)

| Flag | Default | Meaning |
|------|---------|---------|
| `CAN_LISTEN_ONLY` | `1` | Safe sniff beside existing CCMODBUS (no CAN TX) |
| `COOLDOWN_MAP_ENABLE` | `1` | Latch `CoolDown` on F320 pulse until Stop |
| `COOLDOWN_TIMER_ENABLE` | `0` | Set to **`1`** for extra Serial `[NOTE]` lines on timer changes (IR 29 always updated) |
| `COOLDOWN_CAND_WATCH` | `0` | Print `[CAND]` focus-ID changes (discovery only) |
| `RUN_STATE_HUNT_ENABLE` | `0` | Full-bus ID dump / diffs |
| `MODE_HUNT_ENABLE` | `0` | Auto/Manual discovery (mapped; keep off) |

Enable the cool-down timer:

```ini
-DCOOLDOWN_TIMER_ENABLE=1
```

Then status shows e.g. `CoolDown=1  CoolDownTimer=61`.

---

## Modbus (CCMODBUS-compatible)

| Access | PDU | Meaning |
|--------|-----|---------|
| FC04 IR | 25 | Speed (rpm) |
| FC04 IR | 26 | Fuel (‰) |
| FC04 IR | 28 | Battery (dV) |
| FC04 IR | 29 | CoolDownTimer (seconds remaining; **extension**) |
| FC04 IR | 41 / 42 | Engine hours |
| FC01 coils | 1 / 2 | Start / Stop (F320 byte0 **bit6**) |
| FC01 coil | **3** | **CoolDown** (extension; same latch as before) |
| FC01 coil | **4** | **Automatic** — confirmed (F320 byte0 **bit3**) |
| FC01 coil | **5** | **Manual** — confirmed (F320 byte0 **bit2**) |
| FC01 coil | 6 | Locked (CCMODBUS; unmapped) |

### Node-RED

FC1 address **1**, quantity **5** → `[Start, Stop, CoolDown, Automatic, Manual]`

- Qty **2** → `[Start, Stop]` only  
- Qty **3** → `[Start, Stop, CoolDown]` (unchanged from before Auto/Manual)  
- Examples (stopped): Auto `[false, true, false, true, false]`; Manual `[false, true, false, false, true]`  
- Optional CoolDown timer: FC4 address **29**, quantity **1**

### Software control of Auto/Manual later?

**Read path is done** (coils 4/5 from CAN). CCMODBUS **FC05** documents writes on PDF coils 3/4 for Auto/Manual; this build exposes those modes on **4** / **5** so CoolDown stays on **3**. Commanding from Node-RED later needs matching **CAN TX** and `CAN_LISTEN_ONLY=0`. Today writes are local only.
