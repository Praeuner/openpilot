# 2025 Ford F-150 TRON / SecOC Investigation

Investigation into porting bluepilot to a 2025 Ford F-150. Vehicle is detected
correctly as `FORD_F_150_MK14` but locked into dashcam-only mode by Ford's TRON
(SecOC) cryptographic message authentication. This document captures everything
learned so the porting work can resume from a cold start.

## Subject vehicle

- **VIN**: `1FTFW5LD1SFC32496` (position 10 = `S` → 2025 model year)
- **Device**: comma 3X at `10.0.1.125`, dongle `4fde83db16dc0802`
- **bluepilot version**: `2026.001.000` on branch `bp-dev`, commit `51d5de8991508c862e7c51333e5055425b2b8d23`
- **Reference rlog**: `/data/media/0/realdata/000000ce--621a2c5943--9/rlog.zst`

## Initial state

`CarParams` (from device, all three of `CarParams`, `CarParamsCache`,
`CarParamsPersistent`):

```
brand: ford
carFingerprint: FORD_F_150_MK14
dashcamOnly: True
passive: True
safetyConfigs: [(<noOutput enum>, 0)]
```

The fingerprint matches MK14 — no missing-fingerprint problem. The vehicle is
forced into dashcam mode by [opendbc_repo/opendbc/car/ford/interface.py:67-70](../opendbc_repo/opendbc/car/ford/interface.py#L67-L70):

```python
if len(fingerprint[CAN.camera]):
    if fingerprint[CAN.camera].get(0x3d6) != 8 or fingerprint[CAN.camera].get(0x186) != 8:
        carlog.error('dashcamOnly: SecOC is unsupported')
        ret.dashcamOnly = True
```

## SecOC empirical findings

### Authenticator layout

For every protected message, the on-wire frame is 16 bytes:

```
[ 8-byte plaintext payload ][ 1-byte truncated freshness ][ 7-byte truncated MAC ]
```

Confirmed by Ford DBC attributes in
[opendbc_repo/opendbc/dbc/ford_lincoln_base_pt.dbc](../opendbc_repo/opendbc/dbc/ford_lincoln_base_pt.dbc):

```
SCP_FreshnessValueLength   = 64    # full freshness is 64 bits
SCP_FreshnessValueTxLength = 8     # only 8 bits transmitted per msg
SCP_AuthInfoTxLength       = 56    # 56-bit truncated CMAC (7 bytes)
SCP_DataID                 = 0     # default; per-message values not in DBC
```

8 + 56 = 64 bits = the trailing 8 bytes of every authenticated frame.

### Sample frames captured from this truck

```
0x186 (ACCDATA, 50Hz, bus=2 camera tx):
  data: 140401f481f80000   auth: 28 dd85d7b772078a
  data: 140401f481f80000   auth: 29 c4c455d0bc6155
  data: 140401f481f80000   auth: 2a d76884a58dc09c
  data: 140401f481f80000   auth: 2b c03cc5f270c1e9
  data: 140401f481f80000   auth: 2c c212a6ca45e94b
  ...
  data: 140401f481f80000   auth: 32 c004907f10b287
  data: 140401f481f80000   auth: 01 053d561172a55b   ← wraps from 50 → 1
  data: 140401f481f80000   auth: 02 1d58ccaf9313d6

0x3d6 (LateralMotionControl2, 20Hz, bus=2 camera tx):
  data: 01237d0fa2008000   auth: 11 d69dc3b38a1258
  data: 01227d0fa2008002   auth: 12 c8cf17e0113d43
  data: 01217d0fa2008004   auth: 13 d4013e8c795a3b
  data: 01207d0fa2008006   auth: 14 cccb846b43af8a   ← wraps from 20 → 1
  data: 011f7d0fa2008008   auth: 01 1d0804198bc630
```

The first 8 bytes are the plaintext payload (steady state during the captured
segment). The high-entropy trailing bytes change every frame. The first byte of
the trailing 8 is a small counter; the remaining 7 bytes are the truncated CMAC.

### Global 1-second clock — proven empirically

All authenticated messages share a single 1Hz global tick. Counter alignment
captured between `0x186` (50Hz) and `0x3d6` (20Hz):

```
time_s   0x186_cnt   0x3d6_cnt
0.170    49          20         ← both near end of 1s window
0.222    1           1          ← BOTH RESET SIMULTANEOUSLY
0.271    4           2
0.321    6           3
...
1.172    48          20
1.232    1           1          ← reset 1.010s later
```

Conclusion: every SecOC message is slaved to the same 1Hz tick. Each message's
intra-tick counter runs `1..(rate_hz)` then wraps. 1Hz messages always show
counter `01`. This is consistent with AUTOSAR SecOC Profile 3-style freshness:

```
full_freshness = [ persistent_reset_cnt ][ time_tick_since_reset ][ msg_sub_cnt ]
```

with only the bottom 8 bits transmitted. The reset counter and tick counter
must be reconstructed locally; they are NOT broadcast on the bus (see next
section).

### Sync broadcast hunt — no sync message exists

Inspected every unusual-size message on bus 1 looking for an AUTOSAR Freshness
Manager / sync broadcast (would be a low-rate message with monotonic counters
and a MAC field):

```
bus=1 0x009  size=12   constant   000000000000000000007ef4
bus=1 0x020  size=7    constant   00008000000000
bus=1 0x101  size=32   sensor data (slow ADC-like changes in 3 bytes only)
bus=1 0x108  size=32   constant
bus=1 0x109  size=12   sensor data (wheel/RPM-like jitter in a few bytes)
bus=1 0x135  size=24   constant
bus=1 0x4f9  size=20   constant
bus=1 0x005  size=20   ASCII VIN  3146544657354c44315346433332343936...
```

**No SecOC sync broadcast.** Ford ECUs maintain freshness state in NVM and
local timers, synchronized at boot via an out-of-band Freshness Manager that
is not visible on the PT/camera buses. This is stricter than Toyota's design
(Toyota broadcasts `SECOC_SYNCHRONIZATION` with TRIP_CNT, RESET_CNT, MAC over
the wire). Practical implication: a passive listener cannot recover the
current freshness value — must already know it.

### Protected address inventory (this truck)

- **Bus 0 (PT, post-harness)**: 152 unique addresses, **80 are 16-byte
  authenticated**: `0x5a, 0x5c, 0x76, 0x77, 0x7d, 0x82, 0x84, 0x114, 0x115,
  0x165, 0x166, 0x167, 0x168, 0x171, 0x178, 0x186, 0x187, 0x18a, 0x200, 0x202,
  0x204, 0x205, 0x213, 0x216, 0x217, 0x242, 0x25b, 0x331, 0x332, 0x333, 0x334,
  0x33c, 0x33f, 0x37e, 0x38d, 0x3a1, 0x3a2, 0x3a3, 0x3a4, 0x3a6, 0x3a7, 0x3a8,
  0x3a9, 0x3aa, 0x3ab, 0x3ae, 0x3b1, 0x3b3, 0x3ba, 0x3c3, 0x3ca, 0x3cc, 0x3d6,
  0x3d7, 0x3d8, 0x3d9, 0x3eb, 0x3f2, 0x40a, 0x414, 0x415, 0x416, 0x41e, 0x420,
  0x42c, 0x43f, 0x44c, 0x451, 0x4b0, 0x4c7, 0x4c8, 0x4d6, 0x4e0, 0x4e1, 0x4e2,
  0x4e3, 0x4e4, 0x4e5, 0x4e6, 0x4e7`
- **Bus 1 (radar/internal)**: 52 unique addresses, only 4 authenticated:
  `0x21, 0x22, 0x10f, 0x1a4`
- **Bus 2 (camera tx, pre-harness)**: same 80 authenticated as bus 0

### Critical addresses present/absent on this truck

```
0x3d3 (LateralMotionControl, old 8-byte)   NOT PRESENT
0x3d6 (LateralMotionControl2)              16 bytes, SecOC, 20Hz
0x186 (ACCDATA)                            16 bytes, SecOC, 50Hz
0x187 (ACCDATA_2)                          16 bytes, SecOC, 50Hz
0x077 (ACCDATA_3)                          16 bytes, sample looks padded
0x165 (Steering_Data_FD1)                  16 bytes, SecOC
0x3ca                                      16 bytes, SecOC
0x3e6 (Lane_Assist_Data1)                  8 bytes, NOT authenticated
0x092                                      8 bytes, NOT authenticated
```

**Critical**: the older 8-byte `LateralMotionControl` (`0x3d3`) is NOT
transmitted at all on TRON F-150s. The fallback path used on pre-TRON F-150s
([opendbc_repo/opendbc/car/ford/fordcan.py:85](../opendbc_repo/opendbc/car/ford/fordcan.py#L85)) is unavailable on 2025.

### Vehicle FW versions

```
ecu=eps              addr=0x730  RL14-14D003-AC
ecu=shiftByWire      addr=0x732  RL3P-7P470-AH
ecu=abs              addr=0x760  SL34-2D053-AH
ecu=engine           addr=0x7e0  SL3A-14C204-AGH
ecu=fwdRadar         addr=0x764  SJ8T-14D049-AB
ecu=debug            addr=0x7d0  PU5T-14G676-HE
ecu=fwdCamera        addr=0x706  SJ8T-14H102-ABR
ecu=adas             addr=0x730  DSRL14-3F964-AC
ecu=combinationMeter addr=0x7c6  DSML3T-14H031-AJ
ecu=fwdCamera        addr=0x7c4  DSML3T-14H031-AJ
```

The leading character of each part number is the model-year hint (R=2024,
S=2025, P=2023). Mixed-year FWs are normal — modules are sourced over time.

## What's already in the repo (Toyota reference)

The bluepilot codebase already has SecOC infrastructure, used by Toyota
(RAV4 Prime 2021-23, Sienna 4th Gen 2021-23, Yaris 2020/2023). Inventory:

| Piece | File | Status |
|---|---|---|
| CMAC primitive | [opendbc_repo/opendbc/car/secoc.py](../opendbc_repo/opendbc/car/secoc.py) | exists, AUTOSAR Profile 1-style, 28-bit MAC truncation |
| `secOcRequired` / `secOcKeyAvailable` on CarParams | cereal | exists |
| `SecOCKey` param load from `/cache/params/SecOCKey` | [selfdrive/car/card.py:145-164](../selfdrive/car/card.py#L145-L164) | exists |
| Toyota signing in carcontroller | `opendbc_repo/opendbc/car/toyota/carcontroller.py:144-149, 167-172, 277-282` | exists; STEERING_LKA, STEERING_LTA_2, ACC_CONTROL_2 |
| Toyota counter sync | `opendbc_repo/opendbc/car/toyota/carcontroller.py:99-109` | exists; reads `SECOC_SYNCHRONIZATION` from CAN |
| Toyota carstate sync parsing | `opendbc_repo/opendbc/car/toyota/carstate.py:79` | exists |
| Toyota safety pass-through | `opendbc_repo/opendbc/safety/modes/toyota.h` lines 68, 136, 252, 339, 424, 441, 456 | exists; flag `TOYOTA_PARAM_SECOC` |
| Ford SecOC integration | none | **0% — port required** |
| Ford SecOC flag | not in [FordFlags](../opendbc_repo/opendbc/car/ford/values.py#L46-L51) | **missing** |
| Ford safety SecOC | [opendbc_repo/opendbc/safety/modes/ford.h](../opendbc_repo/opendbc/safety/modes/ford.h) | **no SecOC code** |

## Differences vs Toyota that the Ford port must handle

| | Toyota | Ford |
|---|---|---|
| MAC truncation | 28 bits | 56 bits |
| Freshness transmitted per msg | message counter | 1-byte intra-second sub-counter |
| Sync broadcast on bus | yes (`SECOC_SYNCHRONIZATION` w/ TRIP_CNT, RESET_CNT, MAC) | **no** |
| Freshness reconstruction | listen to sync, derive from broadcast | must track ECU's local time-tick from boot, or query state via UDS |
| Authenticated message count | small (3 messages) | large (80 messages on PT bus) |
| `SCP_DataID` per message | known | **unknown**, must reverse-engineer or brute-force from observations once a key is available |

The 56-bit truncation and 1-byte transmitted-freshness fields mean
[opendbc_repo/opendbc/car/secoc.py](../opendbc_repo/opendbc/car/secoc.py)'s `add_mac()` cannot be reused as-is —
needs a Ford variant or a generalized profile parameter.

## Porting plan

A new branch should land these changes. None of them require the SecOC key
to write — only to actually engage in a vehicle.

1. **Add `FordFlags.SECOC`** in [opendbc_repo/opendbc/car/ford/values.py:46-51](../opendbc_repo/opendbc/car/ford/values.py#L46-L51).
   Either set it on a new platform `FORD_F_150_MK14_SECOC` (cleanest) or
   detect it dynamically from FW versions / fingerprint and apply at runtime
   to the existing `FORD_F_150_MK14` (fewer fingerprint edits).

2. **Replace dashcam bailout** at
   [opendbc_repo/opendbc/car/ford/interface.py:67-70](../opendbc_repo/opendbc/car/ford/interface.py#L67-L70). Mirror Toyota's
   pattern: when SecOC is detected, set `ret.secOcRequired = True` and only
   set `dashcamOnly` if `secOcKeyAvailable` is False. The card.py loader at
   [selfdrive/car/card.py:145-164](../selfdrive/car/card.py#L145-L164) already wires the rest.

3. **Generalize `secoc.py`** or add `add_mac_ford(...)`. Inputs:
   - `key`: 16-byte AES
   - `data_id`: 16-bit per-message constant (currently unknown for Ford)
   - `payload`: 8 bytes
   - `freshness`: full 64-bit value `[reset_cnt][time_tick][sub_cnt]`
   - returns: 16-byte frame with 1-byte transmitted freshness + 7-byte MAC.

4. **Ford freshness state tracker** in `opendbc_repo/opendbc/car/ford/carcontroller.py`.
   Maintain `(reset_cnt, time_tick, sub_cnt_per_msg)`. Increment `sub_cnt`
   per TX of each authenticated message. Roll all `sub_cnt`s at the 1-second
   tick boundary, advance `time_tick` by 1.

5. **Bootstrap of `reset_cnt` and `time_tick`** — the actual hard part.
   - Option A: passive sync — listen to incoming authenticated messages
     from the IPMA, brute-force `time_tick` forward from 0 against the
     observed MACs (using the key) until a match is found. Lock in.
   - Option B: UDS query against IPMA for current SecOC freshness state
     (service ID unknown — needs RE).
   - Option C: power-cycle alignment — start the panda at the same instant
     as the IPMA boots so both start from `time_tick = 0`. Fragile.

6. **DBC update** in [opendbc_repo/opendbc/dbc/ford_lincoln_base_pt.dbc](../opendbc_repo/opendbc/dbc/ford_lincoln_base_pt.dbc):
   bump message lengths from 8 to 16 for the 80 protected addresses listed
   above (when SecOC variant is selected — likely needs a new
   `ford_lincoln_secoc_pt.dbc`). Add per-message `SCP_DataID` values once
   reverse-engineered.

7. **Panda safety** in [opendbc_repo/opendbc/safety/modes/ford.h](../opendbc_repo/opendbc/safety/modes/ford.h):
   add `FORD_PARAM_SECOC` flag, allow 16-byte TX of the protected control
   messages, do NOT verify MAC in panda (Toyota doesn't either — application
   layer is trusted to sign correctly).

8. **CarState updates** in `opendbc_repo/opendbc/car/ford/carstate.py`:
   parse 16-byte versions of received messages; the actual signal values
   are still in the first 8 bytes, so the DBC change is sufficient if signal
   definitions stay byte-aligned.

Estimated effort once a key is available: **1-3 weeks of focused work**, using
Toyota as a template. Without a key, steps 1-4, 6-8 are still writable as
"scaffolding" — they just can't be tested end-to-end.

## The blocker: SecOC key acquisition

Nothing above produces a valid MAC without a 16-byte AES-128 key per vehicle,
held in the IPMA (camera) module on TRON Fords. Known paths:

### 1. Hardware key extraction (proven on Toyota, not yet on Ford)

[Willem Melching & Greg Hogan, Hardwear.io USA 2024](https://hardwear.io/usa-2024/speakers/willem-and-greg.php) — extracted SecOC keys
from a Toyota RAV4 Prime PSCM (Renesas RH850/P1M-E) using voltage fault
injection. Documented at [icanhack.nl/blog/secoc-key-extraction](https://icanhack.nl/blog/secoc-key-extraction/) and covered in
[Hackaday](https://hackaday.com/2024/03/08/extracting-secoc-keys-from-a-2021-toyota-rav4-prime/).

For Ford the equivalent attack would target the IPMA (camera) module, which
is the SecOC signer for `0x186` and `0x3d6`. Process:
1. Pull the IPMA from a 2024+ F-150 (matches the target vehicle's MY).
2. Identify the MCU. Likely Infineon AURIX TC3xx or Renesas RH850.
3. Develop or adapt a glitching procedure (ChipWhisperer + custom fixture).
4. Dump firmware via fault injection.
5. Reverse-engineer the SecOC stack to locate key storage.
6. Extract keys. Determine if keys are per-vehicle unique (likely yes per
   AUTOSAR best practice) or shared per-platform (would unlock the model).

Cost: hundreds to a few thousand USD in hardware and modules.
Time: months for someone with the skill set, indefinite for a beginner.
Risk: bricking modules during glitching is normal; budget multiple spares.

**No public Ford SecOC extraction exists at the time of this investigation.**

### 2. UDS-level provisioning via FDRS (theoretical)

Per the [F150 Lightning forum thread](https://www.f150lightningforum.com/forum/threads/adding-acc-lka-to-2025-pro-forscan-tron-encryption-questions.28452/), only Ford's
FDRS dealer tool can run "TRON authorization" (i.e. provision new SecOC keys
into a module after replacement). Forscan cannot. FDRS is unlikely to
authenticate aftermarket hardware. This path would require:

1. Reverse-engineering the FDRS TRON authorization protocol.
2. Either using FDRS directly with a stolen/leased subscription, or
   reimplementing the protocol against the IPMA's Security Access challenge.

Has not been done publicly.

### 3. Wait for someone else (passive)

Active community interest exists. Timeline: unknown. Could be 2026, could
never happen.

## Vehicles WITH TRON (per [bluepilot.dev](https://bluepilot.dev/2025/08/13/confirmed-tron-status-list/), confirmed Aug 2025)

```
Ford Bronco Sport       2025+
Ford Explorer           2025+    (Lincoln Aviator too)
Ford Expedition         2025+    (Lincoln Navigator too)
Ford F-150              2024+    ← THIS VEHICLE
Ford Mustang Coupe      2024+
Ford Mustang Mach-E     2025+
Ford Maverick           2025+
Ford Super Duty         2023+
Ford Transit            2026+
Lincoln Nautilus        2024+
```

Trigger: any vehicle with the new "Phoenix" Ford entertainment system (Android
Auto OS) ships with TRON enabled. TRON-protected ECUs per
[F150 Lightning forum](https://www.f150lightningforum.com/forum/threads/adding-acc-lka-to-2025-pro-forscan-tron-encryption-questions.28452/):
PCM, ABS, DDM, PDM, IPMA, TCU, Gateway. CCM (camera control module) is NOT
part of TRON.

## Vehicles WITHOUT TRON (compatible today)

```
Ford Escape             2023-2025
Ford Expedition         2022-2024
Ford F-150              2021-2023
Ford F-150 Lightning    2022-2025
Ford Mustang Mach-E     2021-2024
Ford Ranger             2024-2025
```

## Bottom line

- The 2025 F-150 fingerprints correctly. The only thing blocking control is
  the absence of a valid SecOC key. The dashcam bailout in
  [interface.py:67-70](../opendbc_repo/opendbc/car/ford/interface.py#L67-L70) is the runtime symptom; the root cause is that
  every relevant control message on the camera bus is cryptographically
  authenticated and the code can't forge MACs without the key.
- Software work needed for full support is significant but tractable
  (1-3 weeks of porting using Toyota as a reference). It is not, however,
  a fix-the-port problem — it is a hardware-security problem dressed up as
  a software problem.
- Stock-longitudinal fallback does not help on this platform because the
  lateral control message (`0x3d6`) is also SecOC-protected.
- The decision tree:
  - Want bluepilot on a Ford this year → buy/keep a 2021-2023 F-150,
    2022-2025 Lightning, or another vehicle from the compatible list above.
  - Want to drive the 2025 forward as a research project → fund or perform
    the hardware key extraction work, then build the scaffolding below.
  - Want to contribute to making 2025+ Fords work for everyone → start with
    the scaffolding (steps 1-4, 6-8) so it's ready when the first key
    appears, and contribute the key extraction work to the community.

## Sources

- [bluepilot.dev — Confirmed TRON Status List (2025-08-13)](https://bluepilot.dev/2025/08/13/confirmed-tron-status-list/)
- [F150 Lightning Forum — Adding ACC/LKA to 2025 Pro, TRON questions](https://www.f150lightningforum.com/forum/threads/adding-acc-lka-to-2025-pro-forscan-tron-encryption-questions.28452/)
- [Hackaday — Extracting SecOC Keys From A 2021 Toyota RAV4 Prime](https://hackaday.com/2024/03/08/extracting-secoc-keys-from-a-2021-toyota-rav4-prime/)
- [icanhack.nl — Extracting Secure Onboard Communication (SecOC) keys from a 2021 Toyota RAV4 Prime](https://icanhack.nl/blog/secoc-key-extraction/)
- [hardwear.io USA 2024 — Willem Melching & Greg Hogan: My car, My keys](https://hardwear.io/usa-2024/speakers/willem-and-greg.php)
- [optskug/docs — openpilot with Toyota/Lexus/Subaru TSK/SecOC](https://github.com/optskug/docs)
- [openpilot PR #23331 — Ford: Initial support for LCA vehicles](https://github.com/commaai/openpilot/pull/23331)
- [Learning Embedded Systems — AUTOSAR SecOC](https://www.learningembeddedsystem.com/automotive/autosar-stacks/autosar-secoc)
