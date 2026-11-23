# nx.js i BLE — co wynika z kodu źródłowego (2026-11-22)

Przeczytane: `@nx.js/runtime` 1.0.0-beta.7 (npm, typy) oraz źródła z gałęzi `main`
repo TooTallNate/nx.js: `packages/runtime/src/navigator/bluetooth.ts`, `source/bluetooth.cc`,
`packages/runtime/src/index.ts`, `packages/runtime/src/timers.ts`.
Wszystko poniżej to wnioski z kodu, część potwierdzona później na sprzęcie (zaznaczone).
Spike w nx.js był w `research/nxjs-spike/` do wersji 1.0.0; jest w historii gita.

## Usługi systemowe, których używa nx.js

- `btm.u` (btmu): skan, `BleConnect`/`BleDisconnect`, stan połączeń, odkrywanie GATT,
  `ConfigureBleMtu` (nx.js prosi o MTU 247), `RegisterBleGattDataPath`.
- `bt`: `LeClientWriteCharacteristic` (z flagą with/without response), read, notify,
  strumień zdarzeń LE (`btRegisterBleEvent`, `btGetLeEventInfo`).
- `btdrv`: tylko „surowy” skan bez filtrów, żeby wypełnić cache stosu przed
  połączeniem po adresie (hbloader daje homebrew dostęp do btdrv).
- **Parametrów połączenia (interwał) nx.js nie czyta i nie ustawia.** W libnx jest
  `btdrv`/`btm` API do tego, ale przez Web Bluetooth nie da się go użyć.

## Skan (to najważniejsze ryzyko dla spike'a)

- Skaner aplikacji na Switchu filtruje po **UUID serwisu w ogłoszeniu**
  („smart device scan”). Nie ma skanu bez filtra.
- Filtr po manufacturer data dopasowuje tylko ładunki z `0x01` jako pierwszym bajtem
  po company ID. Moduł Shell ma `54 52 | 00 3c`, więc **ten filtr nie zadziała**.
- Spike skanuje więc po `0xFFF0`. **Potwierdzone 2026-11-22: nie znajduje auta.**
  nx.js (`uuid_from_string` w `bluetooth.cc`) zawsze buduje filtr o rozmiarze 16 bajtów,
  a moduł ogłasza `FFF0` jako 16-bitowy UUID (iOS pokazuje „Advertised service FFF0”).
  libnx przyjmuje UUID o rozmiarze 2, więc `research/shellscan` (C) sprawdza ten wariant
  oraz filtr po danych producenta.
- **Wynik shellscan, pierwsza wersja (2026-11-22):** wszystkie skany aplikacyjne `btm.u`
  (smart device 16-bit i 128-bit FFF0, general z danymi producenta 0x5452 i 0x5254)
  zwróciły **0 urządzeń, bez błędów**. Wniosek: filtry systemowe odrzucają moduł Shell.
  Następny krok to przebieg E: surowy skan `btdrv` bez filtrów i odczyt zdarzeń skanu
  przez `btdrvGetBleManagedEventInfo`.
- Zapas: `requestDevice({ deviceId: 'aa:bb:…' })` łączy po adresie MAC.
  Spike czyta go z `sdmc:/switch/shellrc/config.json`. iOS nie pokazuje adresu MAC.
  Na Androidzie pokaże go nRF Connect; na Linuksie `bluetoothctl scan le`.
- `requestDevice` zwraca **pierwsze** pasujące urządzenie. Publiczne API nie daje
  listy wyników skanu (natywne `$` jest usuwane z `globalThis`). Ekran wyboru auta
  z listą i RSSI z Fazy 2 w nx.js nie jest więc możliwy bez zmian w runtime.
- Name/`namePrefix` są sprawdzane na wynikach skanu, ale nazwa jest wyciągana
  heurystycznie z surowego rekordu i może jej nie być.

## Zapis

- `writeValueWithoutResponse()` wykonuje natywny zapis **synchronicznie**, przed
  pierwszym `await`. Nie czeka na zdarzenie z potwierdzeniem, więc to tani zapis.
- `writeValueWithResponse()` czeka na zdarzenie z potwierdzeniem. Zdarzenia są
  odpytywane **co 25 ms** (`setInterval(pumpTick, 25)`), więc pomiar RTT ma
  rozdzielczość około 25 ms. Przy 375 ms interwału połączenia różnica i tak będzie
  wyraźna.
- Natywny limit długości zapisu to 600 bajtów (nasze ramki mają 16).

## Timery i pętla

- `processTimers()` jest wołane **raz na klatkę** w `$.onFrame` (index.ts).
  `setInterval(fn, 10)` w praktyce odpala się co klatkę, czyli prawdopodobnie co
  16,7 ms przy 60 Hz. **Ramki co 10 ms w nx.js raczej nie będzie.** Spike to mierzy
  (krok [1]). Dla BLE to zapewne bez znaczenia, bo i tak decyduje interwał
  połączenia, a watchdog modułu ma margines około 0,5 s. Kryterium (a) z sekcji 6
  trzeba więc ocenić na wynikach jazdy, a nie dosłownie.
- `performance.now()` = `Date.now()`, czyli rozdzielczość 1 ms.

## Bezpieczeństwo i cykl życia

- `+` na pierwszym padzie wywołuje `beforeunload`. Spike przejmuje to zdarzenie:
  wysyła ramki neutralne przez około 100 ms, rozłącza się i wychodzi.
- `unload`: pętla zdarzeń już nie działa, ale zapis bez odpowiedzi jest synchroniczny,
  więc da się jeszcze wysłać IDLE. nx.js w swoim handlerze `unload` zamyka BT
  (`btleExit`), a handler jest rejestrowany przy pierwszym użyciu BT. Nasz handler
  musi być zarejestrowany **wcześniej**. Spike robi to na samej górze `main.ts`.
- **Brak zdarzenia utraty fokusu (HOME, uśpienie).** Aplikacja jest po prostu
  wstrzymywana i nie może wysłać IDLE. Ratunkiem jest watchdog modułu: auto staje
  samo w czasie krótszym niż sekunda. Spike wykrywa przerwę w pętli powyżej 200 ms:
  po powrocie wysyła neutral i czeka, aż użytkownik puści przyciski.
  W C/libnx (`appletSetFocusHandlingMode` i hook `OnFocusState`) dałoby się wysłać
  IDLE przed wstrzymaniem. To argument za C w MVP, jeśli watchdog okaże się za wolny.
- Odłączenie pada: `navigator.getGamepads()` przestaje zwracać pad, a spike
  przechodzi wtedy na neutral.
