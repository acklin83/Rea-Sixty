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

## Stufe 2

- `Uf8Units.h` + `test_uf8_units` (33 ctests grün). Die Fläche mit einem UF8 rechnet schon
  über den Kern (Strip-Verschiebung, Bankbreite, Sendfenster, UF1-Slot, FX-/Lanes-Seiten,
  Extender-Umschalter); `uf8Units_()` = 1 bis Stufe 3.
- Entscheid Frank: V-POT am rechten UF8 = links Seite 1, rechts Seite 2.

## Stufe 3

- Tims Fläche (1b + Review-Fixes + CC-Malfix) gegen main gelegt, jeder Hunk gelesen.
- Eigenes: CC-Layer rechts dunkel und taub, Devices-Seite (UF8 L/R, Identify je Gerät, Swap),
  FX/Lanes-Seiten und Extender-Umschalter bis Stufe 4 auf einem Gerät.
- Abweichung vom Plan: Identify lässt das Gerät pulsieren statt LEFT/RIGHT zu schreiben.
- Offen für Stufe 4: rechte Soft-Key-Reihe = nächste Seite, Seiten-LEDs pro Gerät, CC rechts =
  nächste Seite, Plug-in Mode Split/Spread, Extender Mitte.
