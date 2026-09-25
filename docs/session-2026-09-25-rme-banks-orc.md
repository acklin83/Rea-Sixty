# Session 25.09.2026, nachts: die RME-Bänke gehören ORC

Commits `14d7c9c`, `2a29b10`. Deployt `616e66fd` (REAPER lief nicht). ORC aus
`extension/build/ORC.app` neu gestartet, `orc.json` damit auf v49.

## Franks Entscheidungen

- Dim, Mono, Speaker B, Talkback bleiben auf den Soft-Keys 1-4 von Bank 1.
- Jede Bank hat zwei Hälften, **5-8** schaltet um. Bank 1 Hälfte 2: Ext In,
  Main auf Fader (zusätzlich zu MASTER), TotalMix-Fenster (zusätzlich zu
  Nav-Mitte). Bank 2 Snapshots, Bank 3 Layouts, jeweils 1-4 und 5-8.
- Die V-Pot-Bank hängt nicht mehr an 5-8; Nav ◄ ► und Kanal-Encoder reichen.
- Hälfte 2 = Shift-Satz der Bank. SHIFT an der Fläche zeigt sie auch; die
  Tastatur-Shift nur in Rea-Sixty (ORC füttert sie nie in die Engine).
- Der Nutzer baut die Bänke selbst, mit Werksbelegung. Editor in ORC.
- Transporttasten im Side-Car: was ORC dort bindet (DuRec), sonst REAPER.

## Was gebaut wurde

| Teil | Wo |
|---|---|
| Zellen, Hälfte, Druck der RME-Soft-Keys, für beide Programme | `src/RmeSoftKeys.{h,cpp}` |
| Snapshot-/Layout-Auflösung aus `main.cpp` dorthin gezogen, ORC hat sie jetzt auch | `dynSlot`, `loadDyn` |
| Label, Druck und Zelle mit explizitem Satz | `uf1SoftBankKeyLabel`, `dispatchUf1SoftBankSlot`, `uf1sk::staticBankCell` (`mod`, Standard -1) |
| 5-8 schaltet `State::skHalf`, Lampe zeigt Hälfte 2 | `RmeInput.cpp`, `RmeFace.cpp` |
| Bank ◄ ► blättert im Side-Car nicht mehr | `main.cpp`, `uf1SideCarDynPages_` entfernt |
| `rme_ext_in`, sechs `rme_durec_*` (ohne Lampe) | `RmeBuiltins.cpp` |
| Werks-Hälfte 2 von Bank 1, v49 | `seedRmeSideCarBankShift_` |
| Rea-Sixty liest Bänke 10-19 und die fünf Transporttasten aus `orc.json`, schreibt sie nie | `setSideCarSource`, `refreshSideCarSource`, `dispatchSideCarKey`, `serializesUf1Bank_` |
| Rea-Sixty-Settings: Auswahl „Banks for“, Preset „RME Monitor“ und Picker-Kategorie RME weg | `SettingsScreen.cpp`, `Bindings.cpp` |
| ORC-Tab „Soft Keys“: Bank, Hälfte, Art, Name, 4 Labels + Aktionen, Werksbelegung, Transport | `orc/OrcSettingsWindow.mm`, `restoreRmeSideCarBank` |
| Test, gegen drei absichtliche Brüche geprüft | `tests/test_rme_softkeys.cpp` |

## Gemessen, nicht angenommen

- Beide Dateien hatten vorher identische RME-Bänke. v0.6.0 enthält kein
  RME-Side-Car, also keine Kunden-Migration.
- `load()` sät die Werksbelegung UNTER das Geparste: nach einem Laden stehen in
  Bank 10 kurz die Werkstasten, bis `refreshSideCarSource` (1×/s) überschreibt.
  Der Test pinnt das.
- Der Mitschnitt von 19:53 (vor der ITEM-Entfernung) zeigt `ITEM | RME` auf
  `0x0104`. Nach der Entfernung gibt es keinen.

## Offen

- **Frank am Gerät:** RME-Label auf SHIFT+MODE (seit `dd0133f` Taste 1), 5-8 auf
  Bank 1/2/3, SHIFT als Hälfte 2, Snapshots in ORC, der neue ORC-Tab. Den Tab
  konnte ich nicht ansehen: ORC ist ein Menüleisten-Programm und taucht in der
  Freigabeliste für Bildschirmzugriff nicht auf.
- DuRec-Adressen stammen aus der Studie (RME-Blatt 2.1 beta 2), nicht gegen ein
  laufendes DuRec geprüft; `/durec/state` für Lampen ungemessen.
- Transport-Lampen zeigen im Side-Car weiter REAPER, auch wenn DuRec gebunden ist.
- `g_uf1BankSet` in `SettingsScreen.cpp` ist jetzt immer -1; die Zweige dafür
  sind tot und können raus.
- Nach einem Laden der eigenen Datei bis zu 1 s Werksbelegung in Bank 10 (s. o.).

## Nachtrag 22:50: Absturz beim Start, gefixt (`4aeffc3`)

REAPER stürzte 13 s nach dem Start ab: der Pacer-Thread sendete in ein
UF1Device, das der Stale-Handle-Reopen auf dem Hauptthread gerade zerstörte.
Der UF1 hatte sich nach der Übergabe von ORC am USB-Bus neu angemeldet
(`LIBUSB_ERROR_NO_DEVICE`), darum der Reopen. Jetzt schützt `g_uf1DevSwapMx`
jedes Ersetzen des Geräts und jeden Pacer-Zyklus; `openUf1BringUp_`
veröffentlicht das Gerät erst, wenn es offen ist. Deployt `3305a0c9`.
