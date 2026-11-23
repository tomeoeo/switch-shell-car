# Faza 2 — MVP w C, wyniki na sprzęcie (2026-11-22/23)

Źródła: logi aplikacji 0.2.0 i 0.2.1 (`sdmc:/switch/shellrc/shellrc.log`) i historia baterii
z Home Assistanta.

## Połączenie i nadawanie

- **Skan i automatyczne połączenie:** auto znalezione po 0,5–3,4 s surowego skanu. Połączenie
  trwa 14–90 ms, a odkrywanie GATT ~0,9–1,2 s. **Od wybrania auta do jazdy ~1,1 s.**
- **Nadawanie:** odstęp ramek p50 = p95 = p99 = 10,00 ms (wątek z bezwzględnym
  harmonogramem). Brak błędów zapisu, dopóki moduł działa.
- **Bateria:** notify z `...9601` po zapisie CCCD. Pierwszy odczyt przychodzi 3–40 s po
  połączeniu, kolejne co ~60 s. Zdarzenia LE: `op=0x3` to potwierdzenie deskryptora,
  `op=0x4` to notify.
- **Ponowne łączenie:** po zerwaniu aplikacja łączy się sama w 1,4–1,6 s i startuje od
  ramki neutralnej.

## Bezpieczeństwo

- **HOME:** auto staje od razu. Połączenie BLE zrywa się, gdy aplikacja jest w tle.
  Po powrocie aplikacja łączy się sama, a auto stoi, dopóki nie puścisz gałek.
- **Odpięcie pada i uśpienie Switcha:** auto staje (potwierdzone przez użytkownika).
- **Watchdog modułu:** ~0,4 s bez ramek i auto staje samo (Faza 1).

## Jazda

- **15 minut jazdy** na naładowanym module bez zerwania połączenia.
- **Próg ruszania:** praktycznie 20% (10% za słabe). To domyślna wartość od 0.3.0.
- **Skręt przy wolnej jeździe:** bajt prędkości zasila oba silniki, więc przy niskim gazie
  skręt bywał za słaby. Pomoc przy skręcie (150 ms ≥ 60%, potem ≥ 30%) to naprawiła.

## Bateria

- Odczyt pod obciążeniem skacze w dół (w trakcie jazdy 0%, 8%, 21%, a chwilę później
  33–42%). Wiarygodne są odczyty w spoczynku.
- Przy 6–14% (odczyt w czasie jazdy) moduł zrywa łącze. Od wersji 0.2.1 aplikacja
  ostrzega przy ≤ 15%.
- **Czuwanie** (moduł włączony, ogłasza się; HA łączy się na pomiar co 30 min):
  88% → 45% w 10,98 h, czyli **~3,9% na godzinę**. Po ~12 h od pełnego naładowania
  moduł przestał się ogłaszać. Czuwanie wystarcza więc mniej więcej na dobę.

## Definicja ukończenia MVP

| kryterium | stan |
|---|---|
| połączenie < 5 s od wybrania auta | ✅ ~1,1 s |
| jazda we wszystkie strony + turbo | ✅ (turbo = 100%, bajt 9 jest płynną prędkością) |
| bateria na ekranie | ✅ |
| HOME / odłączenie pada / uśpienie zatrzymują auto | ✅ |
| 15 min jazdy bez zerwania | ✅ |
| sam łączy się ponownie, startuje od neutral | ✅ |
| p50/p95 zalogowane | ✅ 10,00 / 10,00 ms |
