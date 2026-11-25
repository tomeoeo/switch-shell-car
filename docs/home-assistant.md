# Home Assistant: the `shell_rc` integration

**English** | [Polski](home-assistant.pl.md)

Home Assistant connects to the car over its own Bluetooth (built-in or an ESPHome proxy).
The Switch is not needed. The integration provides cat mode (the car drives by itself,
randomly and slowly) and keeps an eye on the battery.

Requirements: Home Assistant 2025.2 or newer with a working Bluetooth integration.

## Installation

**From a release:** unzip `shell_rc-ha.zip` (Releases tab) into `/config/custom_components/`
so that you get `/config/custom_components/shell_rc/`. Restart HA.

**With HACS:** HACS → ⋮ menu → Custom repositories → this repository's URL, category
"Integration". Then download "Shell RC" and restart HA.

**From the repository over SSH** (the "Terminal & SSH" add-on): `make deploy-ha HA=ha-address`.

Then switch the car on: HA discovers it by itself (Settings → Devices & services →
"Discovered"), click "Configure". You can also add it manually: Add integration →
"Shell RC". The car is recognized by the manufacturer data in its Bluetooth advertisement
(company `0x5254`, data `00 3c`).

## Entities

| entity | what it does |
|---|---|
| **Cat mode** (switch) | connects, drives randomly, turns itself off and disconnects when the time is up |
| Cat mode duration, minimum / maximum speed | sliders: 120 s and 20–60% by default (kept across restarts) |
| **Stop** (button) | ends cat mode immediately |
| **Battery** (%) | last reading (kept across restarts) |
| Check battery (button) | connects briefly and reads the battery |
| Battery checked (timestamp) | when the last reading was taken |
| **Visible** | whether the car advertised (on and in range) in the last 2 minutes |
| Last seen (timestamp) | the last advertisement, to the minute |
| Status | idle / connecting / cat mode / checking battery / unreachable / error (with a `last_error` attribute) |
| Lights | whether the lights are on while driving |
| Connected, Signal strength | diagnostics (signal strength is disabled by default) |

**Options:** how often to check the battery, in minutes (30 by default, `0` = off). A check
only happens while the car is visible. It takes 1 to 75 s, then the integration disconnects.

Outside of sessions the integration is **disconnected**, so "Connected: off" is normal. The
module accepts one connection at a time: while you drive with the Switch, HA shows
"Unreachable" or "Error" and tries again next time. Nothing bad happens.

## Battery in standby

A switched-on module loses about **4% per hour**, so it lasts roughly a day (measured on a
Marvel 2026 module with a check every 30 minutes). The history of the **Battery** and
**Visible** entities will show it for your car. Readings taken while driving can be low
(voltage sag under load). Readings at rest are the reliable ones.

## Automations

The cat hit a vibration sensor (e.g. an Aqara Vibration Sensor on the scratching post):

```yaml
automation:
  - alias: "Cat is playing - car starts"
    triggers:
      - trigger: state
        entity_id: binary_sensor.scratching_post_vibration
        to: "on"
    conditions:
      - condition: state
        entity_id: binary_sensor.qcar_xxxxxx_visible
        state: "on"
      - condition: state
        entity_id: switch.qcar_xxxxxx_cat_mode
        state: "off"
    actions:
      - action: switch.turn_on
        target:
          entity_id: switch.qcar_xxxxxx_cat_mode
    mode: single
```

Low battery:

```yaml
automation:
  - alias: "Car - low battery"
    triggers:
      - trigger: numeric_state
        entity_id: sensor.qcar_xxxxxx_battery
        below: 20
    actions:
      - action: notify.notify
        data:
          message: "The car has {{ states('sensor.qcar_xxxxxx_battery') }}% battery"
```

Entity IDs depend on the car name and the HA language. Check them in the device settings.

## Range and troubleshooting

- **"Visible: off" although the car is on:** HA doesn't hear the advertisements. Move the
  car closer, or put a Bluetooth proxy in that room (an ESP32 with ESPHome,
  `bluetooth_proxy: active: true`). HA picks the adapter with the better signal by itself.
  About −80 dBm is the limit.
- **Status "Unreachable" / "Error" when cat mode starts:** the car is connected to another
  device (Switch, phone), switched off, or out of range.
- **The session cuts out:** weak signal or low battery (below ~15% the module drops the link).
- **"Characteristic … was not found" in the log:** the car returned an incomplete service
  list. Since 1.1.1 the integration clears its cache and retries by itself; before that only
  an HA restart helped.

Logs: Settings → System → Logs, filter `shell_rc`.
