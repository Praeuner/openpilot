# Ford SecOC (Secure Onboard Communication) Analysis

## Overview

This document details the analysis of SecOC implementation on Ford TRON platform vehicles, specifically the 2025 Ford F-150. The goal is to understand what would be required to enable openpilot/BluePilot active control on SecOC-equipped Ford vehicles.

**Date:** December 2024
**Vehicle:** 2025 Ford F-150
**Platform:** Ford TRON (CANFD with SecOC)
**Status:** Dashcam-only (SecOC blocking active control)

## Current Blocking Issue

When BluePilot detects SecOC-enabled messages, it automatically enters dashcam-only mode:

```python
# From opendbc/car/ford/interface.py
if fingerprint[CAN.camera].get(0x3d6) != 8 or fingerprint[CAN.camera].get(0x186) != 8:
    carlog.error('dashcamOnly: SecOC is unsupported')
    ret.dashcamOnly = True
```

The check fails because SecOC messages are 16 bytes instead of the standard 8 bytes.

## Vehicle Fingerprint Analysis

### Car Parameters
| Parameter | Value |
|-----------|-------|
| Fingerprint | `FORD_F_150_MK14` |
| Dashcam Only | `True` |
| Passive | `True` |
| Openpilot Longitudinal | `False` |

### Firmware Versions
| ECU | Address | Firmware |
|-----|---------|----------|
| EPS | 0x730 | `RL14-14D003-AC` |
| Forward Camera | 0x706 | `SJ8T-14H102-ABG` |
| Forward Radar | 0x764 | `SJ8T-14D049-AB` |
| ABS | 0x760 | `SL34-2D053-AH` |
| Engine | 0x7e0 | `SL3A-14C204-AGH` |
| HUD | 0x720 | `SL3T-14C026-AF` |
| Shift By Wire | 0x732 | `RL3P-7P470-AH` |

### EPS Configuration Bytes
The EPS firmware query (`0x22 0xDE 0x01`) returns configuration bytes:
```
\xff\xff\xff\x00\xff\xff\xff\xff\xff\x03\xff\xff...
```
- Byte 7 (TJA Config): `0xFF` ✓ (Traffic Jam Assist available)
- Byte 8 (LCA Config): `0xFF` ✓ (Lane Centering Assist available)

**Important:** The lateral control APIs are available. SecOC is the ONLY blocker.

## SecOC Message Analysis

### Affected Messages

| Message | Address | Standard Size | SecOC Size | Purpose |
|---------|---------|---------------|------------|---------|
| LateralMotionControl2 | 0x3D6 (982) | 8 bytes | 16 bytes | Steering commands |
| ACCDATA | 0x186 (390) | 8 bytes | 16 bytes | Acceleration/brake commands |

### Message Structure

SecOC messages follow this 16-byte format:
```
[Payload: 8 bytes][Counter: 1 byte][MAC: 7 bytes]
```

### Sample Data: LateralMotionControl2 (0x3D6)

```
Sample 0: 01 19 7d 0f a2 00 80 14 | 11 | 9d da f0 cf 15 48 e7
Sample 1: 01 18 7d 0f a2 00 80 16 | 12 | 86 ba 30 e0 d4 f3 07
Sample 2: 01 17 7d 0f a2 00 80 18 | 13 | 83 f7 5d 5c 18 83 ce
Sample 3: 01 16 7d 0f a2 00 80 1a | 14 | 86 38 0e 95 a9 a7 dd
Sample 4: 01 15 7d 0f a2 00 80 1c | 01 | c2 07 4c ea 83 87 0e
                                    ^^ Counter increments
```

Observations:
- Counter byte (byte 8) increments: `11 → 12 → 13 → 14 → 01` (wraps at some point)
- MAC bytes (bytes 9-15) change completely with each counter increment
- Payload byte 7 also increments (likely contains its own counter/checksum)

### Sample Data: ACCDATA (0x186)

```
Sample 0: 14 04 01 f4 81 f8 00 00 | 29 | 97 a1 75 6c d9 7c 9f
Sample 1: 14 04 01 f4 81 f8 00 00 | 2a | 8a 15 cf f1 1d 64 11
Sample 2: 14 04 01 f4 81 f8 00 00 | 2b | 95 25 5b ae 6e e1 3a
Sample 3: 14 04 01 f4 81 f8 00 00 | 2c | 97 67 6d e4 51 fd e3
Sample 4: 14 04 01 f4 81 f8 00 00 | 2d | 8f fa 26 92 b1 49 ee
                                    ^^ Counter increments
```

Observations:
- Payload remains constant (vehicle stationary)
- Counter byte increments: `29 → 2a → 2b → 2c → 2d`
- MAC changes with each message

## SecOC Algorithm (Reference: Toyota Implementation)

Based on the existing Toyota SecOC implementation in `opendbc/car/secoc.py`, the algorithm is:

### Step 1: Build Freshness Value (48 bits)
```
[Trip Counter: 16 bits][Reset Counter: 20 bits][Message Counter: 8 bits][Reset Flag: 2 bits][Padding: 2 bits]
```

### Step 2: Build Data to Authenticate (96 bits)
```
[Message ID: 16 bits][Payload: 32 bits][Freshness Value: 48 bits]
```

### Step 3: Calculate CMAC
```python
cmac = CMAC.new(key, ciphermod=AES)
cmac.update(to_auth)
mac = cmac.digest().hex()[:7]  # Truncated to 28 bits (7 hex chars)
```

### Step 4: Build Authenticated Message
```
[Payload: 32 bits][Counter Flag: 2 bits][Reset Flag: 2 bits][Authenticator: 28 bits]
```

**Note:** Ford's implementation may differ slightly. The message structure observed suggests:
- 64-bit payload (8 bytes)
- 8-bit counter
- 56-bit MAC (7 bytes)

This differs from Toyota's 32-bit payload + 32-bit auth structure.

## Requirements for Implementation

### 1. AES Key Extraction (Critical Blocker)

The 128-bit AES key is stored in the vehicle's ECU (likely PSCM or IPMA). Methods to obtain:

- **ECU Firmware Dump:** Extract and reverse engineer ECU firmware
- **Memory Read via Diagnostic:** Some ECUs allow memory reads via UDS (unlikely for security-critical data)
- **Side-Channel Attack:** Hardware-based key extraction (requires physical access and specialized equipment)
- **Security Research:** Wait for published research on Ford SecOC

### 2. Synchronization Message

Ford must broadcast trip counter and reset counter values. Need to identify:

- Message address for SECOC_SYNCHRONIZATION
- Signal layout for TRIP_CNT, RESET_CNT
- Sync authenticator for validation

Potential candidates from DBC attributes:
- `SCP_FreshnessValueLength: 64` (confirms 64-bit freshness value)
- `AuthFreshnessCounterSyncAttempt: 2`

### 3. DBC Updates

Create SecOC-specific message definitions:
```
BO_ 982 LateralMotionControl2_SecOC: 16 IPMA_ADAS
 SG_ LatCtl_D2_Rq : ...
 SG_ ... (existing signals)
 SG_ SecOC_Counter : 64|8@1+ (1,0) [0|255] ""
 SG_ SecOC_MAC : 72|56@1+ (1,0) [0|0] ""
```

### 4. Implementation Steps

1. **Key Storage:** Add secure key storage mechanism in openpilot params
2. **Sync Handler:** Parse Ford's sync message to extract counters
3. **Message Signing:** Create `SecOCFord` class similar to `SecOCLong`
4. **Counter Management:** Track message counters, handle resets
5. **Validation:** Verify MAC generation matches vehicle expectations

## CAN Bus Analysis

### Messages on Camera Bus (Bus 2)

Total unique messages observed: 150+

Notable 16-byte messages (potential SecOC):
- 0x05A, 0x05C, 0x076, 0x077, 0x07D (early addresses)
- 0x186 (ACCDATA) ← **Critical**
- 0x3D6 (LateralMotionControl2) ← **Critical**
- Many others in 0x300-0x400 range

8-byte messages (standard, no SecOC):
- 0x04C, 0x07E, 0x081, 0x083, 0x091, 0x092, etc.

## Related Files

| File | Purpose |
|------|---------|
| `opendbc/car/secoc.py` | Core SecOC MAC algorithm |
| `opendbc/sunnypilot/car/toyota/secoc_long.py` | Toyota implementation reference |
| `opendbc/car/ford/interface.py` | Ford SecOC detection (lines 82-87) |
| `opendbc/car/ford/fordcan.py` | Ford message creation |
| `opendbc/dbc/ford_lincoln_base_pt.dbc` | Ford message definitions |

## Community Resources

- **comma.ai Discord:** #ford channel
- **OpenPilot GitHub Issues:** Search "SecOC" or "TRON"
- **Automotive Security Research:** Papers on AUTOSAR SecOC

## Legal and Safety Considerations

1. **Safety Critical:** Incorrect SecOC implementation could cause vehicle control failures
2. **Manufacturer Terms:** Key extraction may violate manufacturer terms of service
3. **Legal Jurisdiction:** Laws vary by region regarding vehicle modification
4. **AUTOSAR Compliance:** Implementation must follow AUTOSAR 4.x standards

## Conclusion

The 2025 Ford F-150 is technically capable of supporting BluePilot (fingerprints correctly, has TJA/LCA APIs), but is blocked solely by SecOC authentication requirements. Enabling support requires:

1. **Primary:** Extraction of the 128-bit AES key from the vehicle
2. **Secondary:** Identification of Ford's sync message format
3. **Tertiary:** Implementation of Ford-specific SecOC signing

Until the AES key extraction method is discovered, Ford TRON platform vehicles will remain in dashcam-only mode.

## Contributing

If you have information about Ford SecOC implementation, key extraction methods, or can help with reverse engineering, please contribute to this research. Contact the BluePilot/OpenPilot community through Discord or GitHub.

---

*Last Updated: December 2024*
*Analysis performed on: 2025 Ford F-150*
*BluePilot Branch: bp-dev*
