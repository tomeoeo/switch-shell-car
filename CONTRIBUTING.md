# Contributing

**English** | [Polski](#polski)

The most valuable thing is **reports from other cars and modules**: other Shell series
(Motorsport 2020–2023, Racing Legends) or modules from other countries. Use the "Other car /
module" issue template and attach `scan.txt` from `research/shellscan` or a capture from a
BLE app on your phone.

## Code

Only Docker is required:

```sh
make test   # C core (gcc) + HA integration (pytest on a real Home Assistant)
make nx     # nx/shellrc.nro
```

- `core/` is portable C with no Switch dependencies. Every protocol or control change has a
  test in `tests-c/test_core.c`. Frame vectors must match [docs/protocol.md](docs/protocol.md).
- `custom_components/shell_rc/protocol.py` is the same protocol in Python, with the same
  vectors in `tests-ha/test_protocol.py`.
- You can check Switch screen changes without a Switch: `make ui-preview` (PNGs in
  `build-host/ui-preview`, Polish and `en-` English variants). New UI strings go through
  `TR("polish", "english")` in ASCII only (the console font has no Polish letters).
- Safety comes before features: every new path that can send motion must start from the
  neutral frame and stop the car when focus, the controller or the connection is lost.

Pull requests: say what hardware you tested on (or that you didn't). GitHub Actions run the
tests and build the `.nro`.

## Polski

Najcenniejsze są **raporty z innych aut i modułów**: kolejne serie Shell (Motorsport
2020–2023, Racing Legends) albo moduły z innych krajów. Użyj szablonu zgłoszenia „Inne auto /
moduł” i dołącz `scan.txt` z `research/shellscan` albo zrzut z aplikacji BLE na telefonie.

### Kod

Wymaga tylko Dockera (`make test`, `make nx` jak wyżej).

- `core/` to przenośne C bez zależności od Switcha. Każda zmiana protokołu albo sterowania
  ma test w `tests-c/test_core.c`. Wektory ramek muszą zgadzać się z
  [docs/protocol.pl.md](docs/protocol.pl.md).
- `custom_components/shell_rc/protocol.py` to ten sam protokół w Pythonie, z tymi samymi
  wektorami w `tests-ha/test_protocol.py`.
- Zmiany wyglądu ekranu Switcha sprawdzisz bez Switcha: `make ui-preview` (PNG w
  `build-host/ui-preview`, wersje polskie i angielskie `en-`). Nowe napisy w UI idą przez
  `TR("po polsku", "english")`, tylko ASCII (czcionka konsoli nie ma polskich liter).
- Bezpieczeństwo jest ważniejsze od funkcji: każda nowa ścieżka, która może wysłać ruch,
  musi startować od ramki neutralnej i zatrzymywać auto przy utracie fokusu, pada
  albo połączenia.

Pull requesty: opisz, na jakim sprzęcie testowałeś (albo że nie testowałeś).
GitHub Actions uruchomi testy i zbuduje `.nro`.
