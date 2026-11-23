# Faza 1 — wyniki na sprzęcie (2026-11-22)

Switch: HOS 20.5.0, Atmosphère 1.10.0, emuMMC, tryb aplikacji, handheld.
Moduł: Marvel 2026, `QCAR-B5FDA2`, MAC **7a:87:25:b5:fd:a2** (adres publiczny, typ 0).
Źródła: logi spike’a nx.js i `shellscan` (`sdmc:/switch/shellrc/`).

## Skan

- Skany aplikacyjne `btm.u` (smart device 16-bit i 128-bit FFF0, general z danymi
  producenta) zwracają **0 urządzeń**, z MissionControl i bez niego.
- Surowy skan `btdrv` bez filtrów i odczyt `btdrvGetBleManagedEventInfo` w pętli
  bez przerw **znajduje auto** (29 zdarzeń skanu w 20 s, 8 urządzeń, auto przy RSSI −40).
  btm-sysmodule zjada większość zdarzeń, więc trzeba odpytywać bez przerw.
- Ogłoszenie auta: flagi `06`, dane producenta `54 52 00 3c 00`, typ AD `0x06`
  (niepełna lista UUID 128-bit) z FFF0 w formie 128-bit. Nazwy nie ma w pakiecie
  ogłoszenia, jest pewnie w scan response.
- MissionControl (`atmosphere/contents/010000000000bd00`) wyłączony na czas testów.
  Nie ustaliliśmy, czy przeszkadzał.

## Połączenie i zapis

| | C / libnx (`shellscan`) | nx.js (`shellrc`, po adresie) |
|---|---|---|
| połączenie | **26 ms** (zaraz po surowym skanie) | 5955 ms (5 s skanu „priming” + połączenie) |
| data path | 16-bit FFF0 wystarcza | 128-bit też działa |
| zapis bez odpowiedzi | 0,18 ms p50, 0,23 ms p95 | ≤1 ms |
| odstęp ramek | **10,18 ms p50, 10,24 ms p95** | 17 ms p50, 18 ms p95, 59 ms p99, max 140 ms (pętla klatek) |
| błędy zapisu | 0/100 | 0 na ~29 000 ramek przez ~9 min jazdy |
| zapis z odpowiedzią | — | ~1516–1535 ms = **limit 1,5 s w nx.js**, zdarzenie potwierdzenia nie dociera |
| notify baterii | — | włączone, ale **żadna wartość nie przyszła przez ~9 min** |

- Jazda ręczna z nx.js działa: wszystkie kierunki, turbo, połączenie trzyma się ~9 min.
- Interwału połączenia nie zmierzyliśmy. RTT w nx.js to tylko jego limit czasu,
  a nie prawdziwy czas odpowiedzi.

## Decyzja stacku

**C/libnx** dla MVP. Powody:
- nx.js nie umie znaleźć auta, bo jego skan działa tylko przez filtry `btm.u`.
  Działa wyłącznie po wpisaniu adresu ręcznie.
- W nx.js nie dochodzą powiadomienia z baterii ani potwierdzenia zapisu. Najpewniej
  UUID 16-bit i 128-bit nie pasują do siebie przy kierowaniu zdarzeń.
- C daje równe 10 ms, połączenie w kilkadziesiąt ms, surowy skan i obsługę utraty fokusu.

Biblioteka `protocol/` (TS) zostaje jako referencja i źródło wektorów testowych.
Driver w C trzeba przetestować tymi samymi wektorami.

## Watchdog, światła, odczucia (2026-11-22)

Test X nagrany telefonem, zmierzony z dźwięku silnika (`research/motor_audio.py`):

| test | wynik |
|---|---|
| W1: jedna ramka PRZÓD | silnik pracuje **0,38 s**, potem staje sam |
| strumień co klatkę, 50, 100, 150, 200, 300 ms | 3,06–3,12 s ciągłej pracy, **bez przerw** |
| strumień co 500 ms | cykl ~0,39 s pracy i ~0,1 s przerwy, czyli **szarpie** |

- **Watchdog modułu ≈ 0,4 s.** Ramki muszą przychodzić częściej niż co ~350 ms.
  Po utracie łącza albo zawieszeniu aplikacji auto staje w czasie poniżej 0,4 s.
  To wystarcza jako siatka bezpieczeństwa przy HOME w nx.js.
- **Światła:** Y je włącza, czyli bajt 8 = `0x00` oznacza światła włączone. Potwierdzone.
- **Jazda:** użytkownik nie czuje szarpania ani opóźnienia (pętla 17 ms w nx.js).
- **Turbo (bajt 9 = `0x64`): różnicy nie czuć.** Do sprawdzenia testem prędkości
  (przycisk R w menu spike’a nx.js, dziś usuniętego z repo: bajt 9 = 80/100/40/100/80/20, nagranie z dźwiękiem).
  Jeśli 40 i 20 też brzmią tak samo, moduł Marvel ignoruje bajt 9 i prędkość ustawia
  się inaczej (być może nowe charakterystyki `02`–`06`).

## Test prędkości — bajt 9 (2026-11-22, nagranie 11:41)

Przód przez 3 s przy kolejnych wartościach bajtu 9, koła w górze. Głośność silnika
(RMS, 16 kHz, bez krawędzi rozpędzania):

| bajt 9 | RMS |
|---|---|
| 20 | 271, silnik **ledwo rusza** (krótkie zrywy, a nie ciągła praca) |
| 40 | 641 |
| 80 | 1298 / 1433 (dwa przebiegi) |
| 100 | 1481 / 1626 (dwa przebiegi) |

- **Bajt 9 to płynna prędkość (procent PWM)**, a nie przełącznik.
- 100 względem 80 to tylko około +13% głośności. Dlatego „turbo” jest słabo wyczuwalne.
- Próg ruszania jest między 20 a 40. Dokładniej trzeba go zmierzyć (np. 25/30/35).
- Częstotliwości dominujące (~300 Hz i ~1430 Hz) nie zmieniają się z prędkością,
  więc do oceny prędkości nadaje się głośność, a nie wysokość dźwięku.
- Wniosek dla MVP: **gaz proporcjonalny**. Wychylenie gałki zamieniamy na bajt 9
  w zakresie od progu ruszania do 100.

## Otwarte

- Dokładny próg ruszania (bajt 9 między 20 a 40).
- Wartości powyżej 100: nie testowane, nie wysyłamy.
- Bateria: w nx.js nie przyszło żadne notify. Sprawdzić w C.
