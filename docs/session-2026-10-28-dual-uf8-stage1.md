# Session 2026-10-28: Zwei UF8, Stufe 1

- `58e93fcf`: Tims `5e45f266` per Cherry-Pick (Autor Tim Shadgett), ohne Konflikte.
- `9a94bceb`: Einstellung „Second UF8“ (Settings → Devices, ExtState `uf8_second_unit`, ab Werk
  aus). Aus: kein zweites Gerät, keine Suche, das UF8 öffnet wie bisher
  (`libusb_open_device_with_vid_pid`). Umschalten öffnet das UF8 neu. Builtin `uf8_swap_units`
  (Hardware Modes) neben Tims Action, beide nur als Anfrage an den Timer; mit einem UF8 kein
  Effekt. Öffnungsfehler rechts nur bei neuem Grund in der Konsole.
- ctest 32/32, `check_builtin_docs.py` grün.

Offen: Frank prüft ein UF8 (Ab-/Anstecken, Einstellung aus und an), Tim prüft zwei Geräte und
Swap. Danach Stufe 2 (`Uf8Units.h` + ctest).
