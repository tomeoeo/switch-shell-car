Sterowanie autkami Shell RC (moduł BLE z serii Marvel 2026) z Nintendo Switcha
i z Home Assistanta. Zmiany: [CHANGELOG.md](https://github.com/tomeoeo/switch-shell-car/blob/v1.0.0/CHANGELOG.md).

**Switch** (`shellrc.nro`, homebrew, Atmosphère):

1. Skopiuj `shellrc.nro` na kartę SD do `/switch/shellrc/shellrc.nro`.
2. Uruchom hbmenu w trybie aplikacji: przytrzymaj R przy starcie dowolnej gry.
3. Wybierz **Shell RC**. Auto musi być włączone i niepołączone z telefonem.

**Home Assistant** (`shell_rc-ha.zip`):

1. Rozpakuj do `/config/custom_components/`, żeby powstało `/config/custom_components/shell_rc/`.
2. Zrestartuj HA. Auto pojawi się jako wykryte urządzenie.

Moduł przyjmuje jedno połączenie naraz: kiedy jeździsz Switchem, HA czeka.
