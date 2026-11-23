# Protokół modułu Shell RC (seria Marvel 2026, „QCAR”)

Wszystko poniżej potwierdzone na module `QCAR-B5FDA2` (USB-C) w dniach 2026-11-22/23.
Ta sama rodzina co Shell Motorsport 2020–2023 (BrandBase). Implementacje: `core/qcar.c`
(Switch), `custom_components/shell_rc/protocol.py` (Home Assistant).

## Ogłoszenie (advertising)

```
02 01 06                     flagi
05 ff 54 52 00 3c            dane producenta: firma 0x5254 (21076), ładunek 00 3c
11 06 fb349b5f...f0ff0000    niepełna lista UUID 128-bit: 0000fff0-0000-1000-8000-00805f9b34fb
```

- Nazwa (`QCAR-` + ostatnie 3 bajty adresu) nie występuje w pakiecie ogłoszenia.
- Adres jest publiczny (typ 0), np. `7a:87:25:b5:fd:a2`.
- **Switch:** skany aplikacyjne `btm.u` tego modułu nie widzą (żaden filtr nie pasuje).
  Działa tylko surowy skan `btdrv` bez filtrów (`nx/source/ble.c`).

## GATT

Serwis `fff0` (16-bit), charakterystyki `d44bc439-abfd-45a2-b575-9254161296xx`:

| xx | właściwości | rola |
|---|---|---|
| `00` | write, write without response | **sterowanie** (16 B, AES) |
| `01` | write, write w/o response, notify | **bateria** (16 B, AES) |
| `02`–`04`, `06`, `0a`, `0b` | write, write w/o response | nieznane, nie zapisujemy |
| `05` | write, write w/o response, notify | nieznane |

Serwis `ae00` (`ae01` write w/o response, `ae02` notify): prawdopodobnie OTA/serwisowy.
Nie ruszamy go.

## Ramka sterowania

AES-128-ECB, klucz `34522a5b7a6e492c08090a9d8d2a23f8`, jeden blok. Tekst jawny:

| bajt | znaczenie |
|---|---|
| 0 | `0x00` |
| 1–3 | `CTL` |
| 4 / 5 | przód / tył (1/0) |
| 6 / 7 | lewo / prawo (1/0) |
| 8 | światła, **odwrócone**: `0x00` = włączone, `0x01` = wyłączone |
| 9 | **prędkość 0–100 (% PWM), płynna**. Oficjalnie 80 = normalna, 100 = turbo |
| 10–15 | `0x00` |

- Ramka neutralna (IDLE): `025e695a48ff2a438ca680f83e04e45d`.
- Bajt prędkości zasila też silnik skrętu: przy < ~30% skręt bywa za słaby.
- Próg ruszania: ~20%.
- **Watchdog modułu:** ~0,38–0,40 s po ostatniej ramce auto staje. Ramki co ≤ 300 ms dają
  płynną jazdę. Używamy 10 ms (Switch) i 50 ms (HA).
- Wysyłanie dwóch sprzecznych kierunków naraz jest niedozwolone. W naszych typach nie da
  się go wyrazić.

## Bateria

Notify z `...01` (po zapisaniu `01 00` do CCCD `2902`). Tekst jawny: `[0]` licznik,
`[1..3]` `VBT`, `[4]` procent. Pierwszy odczyt przychodzi 3–40 s po subskrypcji, potem co ~60 s.

Wektor testowy: `83230ab55f4318b94eaff4696bff1ab1` → licznik 2, 72%.

## Połączenie (Switch, libnx)

1. `btmuRegisterBleGattDataPath` dla 16-bitowego `fff0` (bez tego połączenie się nie kończy).
2. Surowy skan `btdrv`, żeby urządzenie trafiło do cache stosu.
3. `btmuBleConnect`.
4. Zapis: `btLeClientWriteCharacteristic` z UUID dokładnie takimi, jakie zwróciło odkrywanie
   GATT (16-bit zostaje 16-bit), bez odpowiedzi. ~0,2 ms na wywołanie.

Moduł przyjmuje **jedno połączenie naraz**.
