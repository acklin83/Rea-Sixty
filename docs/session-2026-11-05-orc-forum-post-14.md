# Session 05.11.2026: ORC, Punkte aus dem Forum (djp53, Post 14)

Auslöser: Frank wollte die Punkte einzeln freigeben, Plan im Chat, jeder Punkt abgesegnet.

## Gebaut
- **SEL folgt dem Submix.** `setSubmix` schreibt auch `sel[Output]`, die Reihe bleibt. Gilt für SEND/PLUG-IN, Submix-Bank, Nav ◄ ► und den Druck auf den UF1-Pot. Löst die Regel vom 23.09. ab („Selektion bleibt"): auf der Output-Reihe wandert der UF1-Fader bei Nav mit, auch in Rea-Sixty. Frank hat das freigegeben.
- **360-Lampe**, UF8 und UF1: hell bei offenem Einstellungsfenster, sonst gedimmt (Frank: gedimmt, nicht dunkel). Der Timer der App schreibt `Surface::setSettingsShown`.
- **Layoutwechsel von hier.** `Manager::send` fragt nach `/layout/load/` sofort neu, und die nächsten zwei Runden entscheiden je allein (`kLayoutRounds`, `applyNameRound(..., 1)`). Zwei, weil die erste noch das alte Layout erwischen kann. Ein Wechsel am Rechner bleibt bei zwei Runden à 3 s.
- **PAN** (`0x6E`): `Side::panOnPots`, `gainParam` fragt es für Anzeige, Drehen und Drücken. ORC und Rea-Sixty-Side-Car (linker UF8, rechter behält die Bindung).
- **Builtins** `rme_row_input`, `_playback`, `_output`, `_fx`, mit Lampe. LAYER bleibt Modi.
- **Handbuch ORC:** PAN, SEL, 360, Reihen-Actions, versteckte Ausgänge und ALL.

## Bewusst nicht gebaut
- Banner-Schrift: kein Schriftparameter im Rahmen, Frank will auch keinen Ersatz-Banner.
- 3a Layouts: Frank bestätigt 8 Slots, ORC bleibt bei acht.
- Schalter „LAYER = Reihen".

## OSC-Tabelle (rme-audio.de/downloads/globalosc_protocol_b2.zip, Stand 21.07.26)
Keine Adresse für ALL/SUB, Names-Knopf und gespiegelte Ausgänge. ALL/SUB und Mirror werden damit nicht gebaut, Umschalten der Namen vom UF8 geht nicht. `/color` 0 = versteckt, `visibleChannels` filtert das, also zeigt ORC versteckte Kanäle auch mit der Option „hidden channels may be received" nicht. Der Handbuchsatz zu ALL bleibt damit richtig.

## Geprüft
Alle 47 ctests, `check_builtin_docs.py`, `orc_manual.py --check`. Neue Tests: PAN (`test_uf8_rme`), SEL (`test_uf8_rme`, `test_rme_input`), Layout-Runde end to end gegen falsches TotalMix (`test_rme_osc`), Builtins (`test_orc_settings`).

## Nicht geprüft
Am echten TotalMix und am Gerät. Windows-Teil (`webwindow::isShowing`, `OrcTray.cpp`) nur über CI gebaut, nicht gestartet. Ob die erste Runde nach dem Laden einen Kanal kurz fälschlich wegnimmt, ist nicht gemessen.

## Offen
Kein Release. `/Applications/ORC.app` ist ein Dev-Build (Version 1.6.2, Build 12 im Plist, neuer Code), das laufende ORC muss Frank neu starten.
