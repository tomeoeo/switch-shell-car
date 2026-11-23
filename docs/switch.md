# Switch app: settings, API and troubleshooting

**English** | [Polski](switch.pl.md)

Installation and controls: [README](../README.md#switch).

## Files on the SD card

Everything lives in `sdmc:/switch/shellrc/`:

| file | contents |
|---|---|
| `shellrc.nro` | the app |
| `config.json` | settings (created and updated by the app) |
| `shellrc.log` | log of the last run: scan, connections, battery, frame statistics (in Polish) |
| `battery.csv` | battery readings in standby: `unix_time,percent,event` |

## `config.json`

Every key is optional. The app saves the gear, start threshold, standby, language and car
address by itself.

| key | default | meaning |
|---|---|---|
| `deviceId` | — | address of the last car; the app connects to it automatically |
| `minSpeed` | 20 | start threshold in % (10–60); in the app: `-` + up/down |
| `gear` | 2 | gear 0–3 (limit 40/60/80/100%) |
| `steerKickMs` | 150 | how long to raise the speed after steering starts (0 = off) |
| `steerKickSpeed` | 60 | to how many % during that time |
| `steerFloor` | 30 | minimum speed while steering with throttle |
| `catSeconds` | 120 | cat mode duration (10–1800 s) |
| `catMin`, `catMax` | 20, 60 | cat mode speed range in % |
| `keepAwake` | false | standby: the Switch stays awake (in the app: `-` on the list, `Y` while driving) |
| `standbyMinutes` | 15 | in standby: how often to check the car battery, in minutes (0 = never) |
| `httpPort` | 0 | HTTP API port (0 = off) |
| `token` | "" | if set, every HTTP request must include `token=...` |
| `lang` | `en` | UI language: `en` or `pl` (on the car list: ZL). Configs from before 1.1.0 stay Polish |

## HTTP API (optional)

For Home Assistant, the `shell_rc` integration ([home-assistant.md](home-assistant.md)) is
the better choice: it doesn't need the Switch. The API helps when HA has no Bluetooth in
range of the car, but the Switch does.

Turn it on in `config.json` (`"httpPort": 8080`, ideally with a `"token"` too). Requirements:
- the Switch is on, the app is open, and the Switch stays awake (standby);
- the car is switched on;
- the Switch has a fixed IP address on your network.

| request | action |
|---|---|
| `GET /cat/start?s=120` | cat mode for `s` seconds. From the car list it first finds and connects the saved car, and disconnects when done |
| `GET /cat/stop` (or `/stop`) | ends cat mode, the car stops |
| `GET /status` | JSON: `state`, `battery` (-1 = no reading), `batteryAgeS`, `cat`, `catPending`, `catLeft`, `keepAwake`, `standby` |

`state` values do not depend on the UI language:

| `state` | meaning |
|---|---|
| `LISTA` | car list (not connected) |
| `LACZENIE` | connecting |
| `JAZDA` | connected, driving |
| `PONAWIAM` | connection lost, retrying |
| `ROZLACZONE` | disconnected |
| `CZUWANIE` | standby, waiting for the next battery check |
| `POMIAR` | standby, reading the battery |

Example for Home Assistant (`configuration.yaml`):

```yaml
rest_command:
  shellrc_cat_start:
    url: "http://SWITCH_IP:8080/cat/start?s=120&token=TOKEN"
rest:
  - resource: "http://SWITCH_IP:8080/status?token=TOKEN"
    scan_interval: 60
    sensor:
      - name: "Shell RC battery"
        unit_of_measurement: "%"
        device_class: battery
        value_template: "{{ value_json.battery if value_json.battery >= 0 else none }}"
```

## Troubleshooting

| symptom | cause and what to do |
|---|---|
| The car list is empty | The car is off, connected to a phone (close the Shell app) or too far away. Press Y. Bluetooth sysmodules (e.g. MissionControl) can interfere: disable them to test. |
| "Bluetooth is unavailable" | Start the app in application mode (hold R while launching a game), not from the Album. |
| The car buzzes but doesn't move with a light push | Raise the start threshold: `-` + up. |
| Weak steering at low speed | Increase `steerFloor` or `steerKickSpeed`. |
| The connection drops | Low module battery (the screen warns at ≤ 15%) or range. The app reconnects on its own. |
| The car won't move after HOME | On purpose: after you return, release the throttle and steering first, then drive. |

When reporting a problem, attach `shellrc.log`.
