# Changelog / Zmiany

## 1.1.1 — 2026-11-25

Home Assistant only; the Switch app is unchanged (still 1.1.0).

### English
- Fixed: after a failed connection HA could no longer reach the car until it was restarted
  ("Characteristic …9601 was not found!" in the log). An incomplete GATT service list got
  cached, and a failure right after connecting left the module connected to HA, so it stopped
  advertising. Now the cache is cleared and the car is always released, and the integration
  tries once more with a fresh service discovery.

### Polski
- Naprawione: po nieudanym połączeniu HA nie mógł się już połączyć z autem aż do restartu
  („Characteristic …9601 was not found!” w logu). Niepełna lista usług GATT trafiała do
  cache, a błąd tuż po połączeniu zostawiał moduł połączony z HA, więc przestawał się
  ogłaszać. Teraz cache jest czyszczony, auto zawsze zwalniane, a integracja próbuje jeszcze
  raz ze świeżym odczytem usług.

## 1.1.0 — 2026-11-23

### English
- The Switch app speaks English: English is the default for new installs, ZL on the car list
  switches between English and Polish, and the choice is saved as `lang` in `config.json`.
  Existing configs (from 1.0.0) stay Polish until you press ZL.
- `GET /status` keeps the same `state` values in both languages.
- English documentation: [README.md](README.md), [docs/switch.md](docs/switch.md),
  [docs/home-assistant.md](docs/home-assistant.md), [docs/protocol.md](docs/protocol.md).
  The Polish versions moved to `*.pl.md`.
- Bilingual contributing guide, issue templates and release notes.

### Polski
- Aplikacja na Switcha mówi po angielsku: nowe instalacje startują po angielsku, ZL na liście
  aut przełącza angielski i polski, a wybór zapisuje się jako `lang` w `config.json`.
  Istniejące konfiguracje (z 1.0.0) zostają po polsku, dopóki nie naciśniesz ZL.
- `GET /status` zwraca te same wartości `state` w obu językach.
- Dokumentacja po angielsku. Polskie wersje są teraz w plikach `*.pl.md`
  ([README.pl.md](README.pl.md)).
- Dwujęzyczne CONTRIBUTING, szablony zgłoszeń i opis wydania.

## 1.0.0 — 2026-11-23

Pierwsze wydanie. Przetestowane na module Marvel 2026 (`QCAR-B5FDA2`), Switchu z HOS 20.5.0
i Atmosphère 1.10.0 oraz Home Assistancie OS z wbudowanym Bluetooth.

### Switch (`shellrc.nro`)
- Wyszukiwanie aut surowym skanem BLE (systemowe skany ich nie widzą) i automatyczne
  łączenie z ostatnim autem, ok. 1 s od znalezienia.
- Płynny gaz na lewej gałce (od progu ruszania do limitu biegu), ZR/ZL pełny gaz i wsteczny,
  4 biegi (40/60/80/100%), boost na B, światła na A, kalibracja progu ruszania.
- Pomoc przy skręcie przy wolnej jeździe.
- Ramka co 10 ms z osobnego wątku. Bateria na ekranie z ostrzeżeniem ≤ 15%.
- Bezpieczeństwo: start zawsze od ramki neutralnej; HOME, uśpienie, odpięcie pada i zerwanie
  połączenia zatrzymują auto; samo łączy się ponownie.
- Tryb kot: losowa, wolna jazda (domyślnie 2 min, 20–60%), przerywana ruchem pada.
- Opcjonalnie (domyślnie wyłączone) API HTTP i czuwanie z pomiarem baterii.
- Nowy wygląd: wskaźnik gazu z zerem pośrodku, pasek stanu w kolorze połączenia, ikona.

### Home Assistant (`custom_components/shell_rc`)
- Wykrywanie auta przez Bluetooth HA, bez Switcha.
- Tryb kot (przełącznik, czas i zakres prędkości), Stop, bateria (pomiar co 30 min
  i na żądanie), widoczność i ostatnio widziane, stan, światła.

### Projekt
- Licencja MIT, dokumentacja dla nowych użytkowników (`docs/switch.md`,
  `docs/home-assistant.md`, `docs/protocol.md`), szablony zgłoszeń, CI z wydaniami.

### Znane ograniczenia
- Moduł przyjmuje jedno połączenie naraz (Switch albo HA).
- Bateria w czuwaniu spada o ~4% na godzinę, więc wystarcza mniej więcej na dobę.
- Moduł nie raportuje oporu silnika: wykrywanie zderzeń jest niemożliwe.
