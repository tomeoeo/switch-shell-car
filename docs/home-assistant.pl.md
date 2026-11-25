# Home Assistant: integracja `shell_rc`

[English](home-assistant.md) | **Polski**

Home Assistant łączy się z autem przez własny Bluetooth (wbudowany albo proxy ESPHome).
Switch nie jest do tego potrzebny. Integracja daje tryb kot (auto jeździ samo, losowo
i wolno) oraz pilnuje baterii.

Wymagania: Home Assistant 2025.2 lub nowszy z działającą integracją Bluetooth.

## Instalacja

**Z wydania:** rozpakuj `shell_rc-ha.zip` (zakładka Releases) do `/config/custom_components/`,
żeby powstał katalog `/config/custom_components/shell_rc/`. Zrestartuj HA.

**Przez HACS:** HACS → menu ⋮ → Własne repozytoria → adres tego repo, kategoria
„Integration”. Potem pobierz „Shell RC” i zrestartuj HA.

**Z repo przez SSH** (dodatek „Terminal & SSH”): `make deploy-ha HA=adres-ha`.

Potem włącz auto: HA sam je wykryje (Ustawienia → Urządzenia i usługi → „Wykryto”), kliknij
„Skonfiguruj”. Możesz też dodać je ręcznie: Dodaj integrację → „Shell RC”. Auto rozpoznawane
jest po danych producenta w ogłoszeniu Bluetooth (firma `0x5254`, dane `00 3c`).

## Encje

| encja | co robi |
|---|---|
| **Tryb kot** (przełącznik) | łączy się, jeździ losowo, po czasie sam się wyłącza i rozłącza |
| Czas trybu kot, prędkość min / max | suwaki: domyślnie 120 s i 20–60% (pamiętane po restarcie) |
| **Stop** (przycisk) | natychmiast kończy tryb kot |
| **Bateria** (%) | ostatni odczyt (pamiętany po restarcie) |
| Sprawdź baterię (przycisk) | łączy się na chwilę i czyta baterię |
| Pomiar baterii (czas) | kiedy był ostatni odczyt |
| **Widoczne** | czy auto się ogłasza (włączone i w zasięgu) w ostatnich 2 minutach |
| Ostatnio widziane (czas) | ostatnie ogłoszenie, z dokładnością do minuty |
| Stan | czeka / łączenie / tryb kot / pomiar baterii / niedostępne / błąd (z atrybutem `last_error`) |
| Światła | czy światła mają być włączone w trakcie jazdy |
| Połączone, Siła sygnału | diagnostyka (siła sygnału domyślnie wyłączona) |

**Opcje:** co ile minut sprawdzać baterię (domyślnie 30, `0` = wyłączone). Pomiar odbywa
się tylko wtedy, gdy auto jest widoczne. Trwa od 1 do 75 s, potem integracja się rozłącza.

Poza sesjami integracja jest **rozłączona**, więc „Połączone: nie” to normalny stan. Moduł
przyjmuje jedno połączenie naraz: kiedy jeździsz Switchem, HA pokaże „Niedostępne” albo
„Błąd” i spróbuje znowu przy następnej okazji. Nic złego się nie dzieje.

## Bateria w czuwaniu

Włączony moduł traci około **4% na godzinę**, więc wytrzymuje mniej więcej dobę (zmierzone
na module Marvel 2026 z pomiarem co 30 minut). Historia encji **Bateria** i **Widoczne**
pokaże to u Ciebie. Odczyty w trakcie jazdy bywają zaniżone (spadek napięcia pod obciążeniem).
Wiarygodne są odczyty w spoczynku.

## Automatyzacje

Kot pacnął czujnik drgań (np. Aqara Vibration Sensor na drapaku):

```yaml
automation:
  - alias: "Kot się bawi - auto rusza"
    triggers:
      - trigger: state
        entity_id: binary_sensor.drapak_wibracja
        to: "on"
    conditions:
      - condition: state
        entity_id: binary_sensor.qcar_xxxxxx_widoczne
        state: "on"
      - condition: state
        entity_id: switch.qcar_xxxxxx_tryb_kot
        state: "off"
    actions:
      - action: switch.turn_on
        target:
          entity_id: switch.qcar_xxxxxx_tryb_kot
    mode: single
```

Słaba bateria:

```yaml
automation:
  - alias: "Autko - słaba bateria"
    triggers:
      - trigger: numeric_state
        entity_id: sensor.qcar_xxxxxx_bateria
        below: 20
    actions:
      - action: notify.notify
        data:
          message: "Autko ma {{ states('sensor.qcar_xxxxxx_bateria') }}% baterii"
```

Identyfikatory encji zależą od nazwy auta i języka HA. Sprawdź je w ustawieniach urządzenia.

## Zasięg i problemy

- **„Widoczne: nie”, choć auto jest włączone:** HA nie słyszy ogłoszeń. Przenieś auto
  bliżej albo postaw w tym pokoju proxy Bluetooth (ESP32 z ESPHome,
  `bluetooth_proxy: active: true`). HA sam wybierze adapter z lepszym sygnałem. Około −80 dBm
  to granica.
- **Stan „Niedostępne” / „Błąd” przy starcie trybu kot:** auto jest połączone z innym
  urządzeniem (Switch, telefon), wyłączone albo poza zasięgiem.
- **Sesja się urywa:** słaby sygnał albo słaba bateria (poniżej ~15% moduł zrywa łącze).
- **„Characteristic … was not found” w logu:** auto zwróciło niepełną listę usług. Od 1.1.1
  integracja sama czyści cache i próbuje ponownie; wcześniej pomagał tylko restart HA.

Logi: Ustawienia → System → Dzienniki, filtr `shell_rc`.
