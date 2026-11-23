# Aplikacja na Switcha: konfiguracja, API i problemy

Instalacja i sterowanie: [README](../README.md#switch).

## Pliki na karcie SD

Wszystko leży w `sdmc:/switch/shellrc/`:

| plik | zawartość |
|---|---|
| `shellrc.nro` | aplikacja |
| `config.json` | ustawienia (tworzone i aktualizowane przez aplikację) |
| `shellrc.log` | log ostatniego uruchomienia: skan, połączenia, bateria, statystyki ramek |
| `battery.csv` | pomiary baterii w czuwaniu: `czas_unix,procent,zdarzenie` |

## `config.json`

Każdy klucz jest opcjonalny. Bieg, próg ruszania, czuwanie i adres auta aplikacja zapisuje
sama.

| klucz | domyślnie | znaczenie |
|---|---|---|
| `deviceId` | — | adres ostatniego auta; z nim łączy się automatycznie |
| `minSpeed` | 20 | próg ruszania w % (10–60); w aplikacji: `-` + góra/dół |
| `gear` | 2 | bieg 0–3 (limit 40/60/80/100%) |
| `steerKickMs` | 150 | jak długo po rozpoczęciu skrętu podnosić prędkość (0 = wyłączone) |
| `steerKickSpeed` | 60 | do ilu % w tym czasie |
| `steerFloor` | 30 | minimalna prędkość w trakcie skrętu z gazem |
| `catSeconds` | 120 | czas trybu kot (10–1800 s) |
| `catMin`, `catMax` | 20, 60 | zakres prędkości trybu kot w % |
| `keepAwake` | false | czuwanie: Switch nie zasypia (w aplikacji: `-` na liście, `Y` w jeździe) |
| `standbyMinutes` | 15 | w czuwaniu: co ile minut sprawdzać baterię auta (0 = nie sprawdzaj) |
| `httpPort` | 0 | port API HTTP (0 = wyłączone) |
| `token` | "" | jeśli ustawiony, każde zapytanie HTTP musi mieć `token=...` |

## API HTTP (opcjonalne)

Do Home Assistanta lepiej użyć integracji `shell_rc` ([home-assistant.md](home-assistant.md)),
która nie potrzebuje Switcha. API przydaje się, gdy HA nie ma Bluetootha w zasięgu auta, a Switch ma.

Włącz je w `config.json` (`"httpPort": 8080`, najlepiej też z `"token"`). Wymagania:
- Switch jest włączony, z otwartą aplikacją, i nie zasypia (czuwanie);
- auto jest włączone;
- Switch ma stały adres IP w sieci.

| zapytanie | działanie |
|---|---|
| `GET /cat/start?s=120` | tryb kot przez `s` sekund. Z listy aut: najpierw znajduje i łączy zapisane auto, a po zakończeniu rozłącza |
| `GET /cat/stop` (albo `/stop`) | koniec trybu kot, auto staje |
| `GET /status` | JSON: `state`, `battery` (-1 = brak odczytu), `batteryAgeS`, `cat`, `catPending`, `catLeft`, `keepAwake`, `standby` |

Przykład dla Home Assistanta (`configuration.yaml`):

```yaml
rest_command:
  shellrc_kot_start:
    url: "http://SWITCH_IP:8080/cat/start?s=120&token=TOKEN"
rest:
  - resource: "http://SWITCH_IP:8080/status?token=TOKEN"
    scan_interval: 60
    sensor:
      - name: "Shell RC bateria"
        unit_of_measurement: "%"
        device_class: battery
        value_template: "{{ value_json.battery if value_json.battery >= 0 else none }}"
```

## Problemy

| objaw | przyczyna i co zrobić |
|---|---|
| Lista aut pusta | Auto wyłączone, połączone z telefonem (zamknij aplikację Shell) albo za daleko. Naciśnij Y. Sysmoduły Bluetooth (np. MissionControl) mogą przeszkadzać: wyłącz je na próbę. |
| „Bluetooth jest niedostępny” | Uruchom aplikację w trybie aplikacji (przytrzymaj R przy starcie gry), nie z Albumu. |
| Auto brzęczy, ale nie rusza przy lekkim wychyleniu | Podnieś próg ruszania: `-` + góra. |
| Słabo skręca przy wolnej jeździe | Zwiększ `steerFloor` albo `steerKickSpeed`. |
| Połączenie się zrywa | Słaba bateria modułu (ekran ostrzega przy ≤ 15%) albo zasięg. Aplikacja łączy się ponownie sama. |
| Po HOME auto nie rusza | Celowo: po powrocie puść gaz i skręt, dopiero potem jedź. |

Zgłaszając problem, dołącz `shellrc.log`.
