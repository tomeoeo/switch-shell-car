# Zmiany

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
