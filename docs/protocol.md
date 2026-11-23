# Shell RC module protocol (Marvel 2026 series, "QCAR")

**English** | [Polski](protocol.pl.md)

Everything below was confirmed on the `QCAR-B5FDA2` module (USB-C) on 2026-11-22/23. It is
the same family as Shell Motorsport 2020–2023 (BrandBase). Implementations: `core/qcar.c`
(Switch), `custom_components/shell_rc/protocol.py` (Home Assistant).

## Advertising

```
02 01 06                     flags
05 ff 54 52 00 3c            manufacturer data: company 0x5254 (21076), payload 00 3c
11 06 fb349b5f...f0ff0000    incomplete list of 128-bit UUIDs: 0000fff0-0000-1000-8000-00805f9b34fb
```

- The name (`QCAR-` + the last 3 bytes of the address) is not in the advertising packet.
- The address is public (type 0), e.g. `7a:87:25:b5:fd:a2`.
- **Switch:** application-level `btm.u` scans never see this module (no filter matches).
  Only a raw `btdrv` scan with filters disabled works (`nx/source/ble.c`).

## GATT

Service `fff0` (16-bit), characteristics `d44bc439-abfd-45a2-b575-9254161296xx`:

| xx | properties | role |
|---|---|---|
| `00` | write, write without response | **control** (16 B, AES) |
| `01` | write, write w/o response, notify | **battery** (16 B, AES) |
| `02`–`04`, `06`, `0a`, `0b` | write, write w/o response | unknown, we don't write them |
| `05` | write, write w/o response, notify | unknown |

Service `ae00` (`ae01` write w/o response, `ae02` notify): probably OTA/service. We leave it
alone.

## Control frame

AES-128-ECB, key `34522a5b7a6e492c08090a9d8d2a23f8`, one block. Plaintext:

| byte | meaning |
|---|---|
| 0 | `0x00` |
| 1–3 | `CTL` |
| 4 / 5 | forward / back (1/0) |
| 6 / 7 | left / right (1/0) |
| 8 | lights, **inverted**: `0x00` = on, `0x01` = off |
| 9 | **speed 0–100 (% PWM), proportional**. Officially 80 = normal, 100 = turbo |
| 10–15 | `0x00` |

- Neutral (IDLE) frame: `025e695a48ff2a438ca680f83e04e45d`.
- The speed byte also powers the steering motor: below ~30% steering can be too weak.
- Start threshold: ~20%.
- **Module watchdog:** the car stops ~0.38–0.40 s after the last frame. Frames every
  ≤ 300 ms give smooth driving. We use 10 ms (Switch) and 50 ms (HA).
- Sending two opposite directions at once is not allowed. Our types cannot express it.

## Battery

Notify from `...01` (after writing `01 00` to CCCD `2902`). Plaintext: `[0]` counter,
`[1..3]` `VBT`, `[4]` percent. The first reading arrives 3–40 s after subscribing, then
every ~60 s.

Test vector: `83230ab55f4318b94eaff4696bff1ab1` → counter 2, 72%.

## Connecting (Switch, libnx)

1. `btmuRegisterBleGattDataPath` for the 16-bit `fff0` (without it the connection never completes).
2. A raw `btdrv` scan, so the device lands in the stack's cache.
3. `btmuBleConnect`.
4. Writes: `btLeClientWriteCharacteristic` with UUIDs exactly as GATT discovery returned
   them (16-bit stays 16-bit), without response. ~0.2 ms per call.

The module accepts **one connection at a time**.
