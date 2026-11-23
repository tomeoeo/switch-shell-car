Drive Shell RC toy cars (BLE module, Marvel 2026 series) from a Nintendo Switch and from
Home Assistant. Changes: [CHANGELOG.md](https://github.com/tomeoeo/switch-shell-car/blob/main/CHANGELOG.md).
Polski: [README.pl.md](https://github.com/tomeoeo/switch-shell-car/blob/main/README.pl.md).

**Switch** (`shellrc.nro`, homebrew, Atmosphère):

1. Copy `shellrc.nro` to the SD card as `/switch/shellrc/shellrc.nro`.
2. Start hbmenu in application mode: hold R while launching any game.
3. Pick **Shell RC**. The car must be on and not connected to a phone.
   ZL on the car list switches the language (English / Polish).

**Home Assistant** (`shell_rc-ha.zip`):

1. Unzip into `/config/custom_components/` so that you get `/config/custom_components/shell_rc/`.
2. Restart HA. The car shows up as a discovered device.

The module accepts one connection at a time: while you drive with the Switch, HA waits.
