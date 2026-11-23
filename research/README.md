# research/

**English** | [Polski](#polski)

Tools from reverse-engineering the protocol. You don't need them to build or use the app,
but they help with new modules or when something doesn't work.

| file | what it is |
|---|---|
| `shellscan/` | C/libnx diagnostics: every Switch BLE scan mode, a raw `btdrv` scan and a connection test (it only sends neutral frames). Output: `sdmc:/switch/shellrc/scan.txt`. This is how we found the car. |
| `motor_audio.py` | analyzes a phone recording (wheels in the air): when and how loud the motor runs. This is how we measured the watchdog (~0.4 s) and found that byte 9 is a proportional speed. |

Results and decisions (Polish): [../docs/research/](../docs/research/). The first version of
the app was written in nx.js (TypeScript) and abandoned, see
[nxjs-ble-notes.md](../docs/research/nxjs-ble-notes.md).

```sh
# shellscan (devkitPro image), then copy research/shellscan/shellscan.nro to the SD card
docker run --rm -v "$PWD":/work -w /work devkitpro/devkita64:20260219 make -C research/shellscan

# recording analysis (needs ffmpeg, numpy optional)
python3 research/motor_audio.py recording.mp4
```

## Polski

Narzędzia z rozpracowywania protokołu. Nie są potrzebne do zbudowania ani używania aplikacji,
ale przydają się przy nowych modułach albo gdy coś nie działa.

| plik | co to jest |
|---|---|
| `shellscan/` | diagnostyka w C/libnx: wszystkie tryby skanu BLE Switcha, surowy skan `btdrv` i test połączenia (wysyła tylko ramki neutralne). Wynik: `sdmc:/switch/shellrc/scan.txt`. Tak znaleźliśmy auto. |
| `motor_audio.py` | analiza nagrania z telefonu (koła w górze): kiedy i jak głośno pracuje silnik. Tak zmierzyliśmy watchdog (~0,4 s) i to, że bajt 9 to płynna prędkość. |

Wyniki i decyzje: [../docs/research/](../docs/research/). Pierwsza wersja aplikacji powstała
w nx.js (TypeScript) i została porzucona, zob.
[nxjs-ble-notes.md](../docs/research/nxjs-ble-notes.md).

Polecenia jak wyżej (`nagranie.mp4` zamiast `recording.mp4`).
