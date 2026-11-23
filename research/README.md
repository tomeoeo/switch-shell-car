# research/

Narzędzia z rozpracowywania protokołu. Nie są potrzebne do zbudowania ani używania aplikacji,
ale przydają się przy nowych modułach albo gdy coś nie działa.

| plik | co to jest |
|---|---|
| `shellscan/` | diagnostyka w C/libnx: wszystkie tryby skanu BLE Switcha, surowy skan `btdrv` i test połączenia (wysyła tylko ramki neutralne). Wynik: `sdmc:/switch/shellrc/scan.txt`. Tak znaleźliśmy auto. |
| `motor_audio.py` | analiza nagrania z telefonu (koła w górze): kiedy i jak głośno pracuje silnik. Tak zmierzyliśmy watchdog (~0,4 s) i to, że bajt 9 to płynna prędkość. |

Wyniki i decyzje: [../docs/research/](../docs/research/). Pierwsza wersja aplikacji
w nx.js (TypeScript) jest w historii gita, w wersji 1.0.0 w katalogu `research/nxjs-spike/`.

```sh
# shellscan (obraz devkitPro), potem skopiuj research/shellscan/shellscan.nro na kartę
docker run --rm -v "$PWD":/work -w /work devkitpro/devkita64:20260219 make -C research/shellscan

# analiza nagrania (potrzebuje ffmpeg, opcjonalnie numpy)
python3 research/motor_audio.py nagranie.mp4
```
