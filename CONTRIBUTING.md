# Jak pomóc

Najcenniejsze są **raporty z innych aut i modułów**: kolejne serie Shell (Motorsport
2020–2023, Racing Legends) albo moduły z innych krajów. Użyj szablonu zgłoszenia „Inne auto /
moduł” i dołącz `scan.txt` z `research/shellscan` albo zrzut z aplikacji BLE na telefonie.

## Kod

Wymaga tylko Dockera:

```sh
make test   # rdzeń w C (gcc) + integracja HA (pytest na prawdziwym Home Assistancie)
make nx     # nx/shellrc.nro
```

- `core/` to przenośne C bez zależności od Switcha. Każda zmiana protokołu albo sterowania
  ma test w `tests-c/test_core.c`. Wektory ramek muszą zgadzać się z `docs/protocol.md`.
- `custom_components/shell_rc/protocol.py` to ten sam protokół w Pythonie, z tymi samymi
  wektorami w `tests-ha/test_protocol.py`.
- Zmiany wyglądu ekranu Switcha sprawdzisz bez Switcha: `make ui-preview` (PNG w `build-host/ui-preview`).
- Bezpieczeństwo jest ważniejsze od funkcji: każda nowa ścieżka, która może wysłać ruch,
  musi startować od ramki neutralnej i zatrzymywać auto przy utracie fokusu, pada
  albo połączenia.

Pull requesty: opisz, na jakim sprzęcie testowałeś (albo że nie testowałeś).
GitHub Actions uruchomi testy i zbuduje `.nro`.
