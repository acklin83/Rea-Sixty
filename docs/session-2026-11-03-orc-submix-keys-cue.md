# Session 03.11.2026: UF8-Submix-Tasten, Submix-Bank, Cue

Auslöser: djp53 (Dave) im RME-Forum, Thread ORC, Posts 10 und 11. Frank: bauen, weil es den UF8 zum besten Live-Controller für TotalMix macht.

## Gebaut
- SEND/PLUG-IN 1-8: je ein Ausgang, `Config::uf8SubmixKeys`, `rme.json` Block `uf8.submixKeys`, ORC-Reiter UF8. Werk = bisherige Belegung. Kein Versions-Bump, alte Dateien behalten das Werk.
- `rmeu::keyOutput`: versteckt, rechte Hälfte, kein Ausgang = dunkel und tot. War ein Fehler: eine versteckte Rolle leuchtete, der Druck landete still auf Main (`effectiveSubmix`). Phones 3/4 hängen bei Frank am Snapshot.
- Bankart `RmeSubmixes = 15`. Ausgang steckt im Slot (`ActionSlot::textParam`, Aktion `rme_submix`, damit der Serialiser den Slot behält; gefeuert wird sie nie). Banken 10.. liegen in orc.json, Rea-Sixty liest sie, darum keine Liste in Rea-Sixty.
- Cue: `/controlroom/cuechan`. SOLO auf Ausgängen (Frank, statt des ersten Shift + SEL), Shift + SEND/PLUG-IN, Shift + Taste der Submix-Bank. Submix bleibt. Gelb, Helligkeit weiter = Submix. SOLO-Lampe auf Ausgängen = Cue. UF1 gleich.
- Shift: ORC UF8 = FINE (0x6F), Tastatur nur beim Druck (`orc/OrcKeyboard.h`), kein Spiegel. Rea-Sixty `shiftHeldAnywhere_()`.
- Handbuch ORC: Abschnitt Cue, Soft-Key-Bänke, UF8, Reiter.

## Gemessen (Frank, Remote 7005/7006)
- `cuechan=8` kommt an, `-1` löscht, beides im nächsten `/sendall` gelesen. Kein Echo an den schreibenden Remote.
- Submix-Auswahl von TotalMix: keine Adresse, auch mit „Submix linked to OSC controller 3“ still. Follow gestrichen (Frank: „ist eh flexibler ohne“).

## Aus Franks Test
- Menüs zeigten Main, Main B und Phones doppelt: `displayName` nennt einen Ausgang mit Rolle nach der Rolle, die Liste führte ihn zusätzlich unter Outputs. Jetzt nur noch als Rolle (`rmeu::outputsWithoutRole`). Ein gespeicherter fester Ausgang, der inzwischen eine Rolle hat, zeigt die Rolle (`rmeu::menuSpec`), die Datei bleibt.
- Soft-Key der Submix-Bank wurde in ORC am UF8 nicht gelb: `topKey` im Cache von `paintStrip` kennt nur den Zustand. `forgetTopKeyOnColour` vor dem Malen, mit Test. Rea-Sixty hatte den Fehler nicht, sein Cache ist ein Composite mit Farbe.

## Offen
- Die neuen ORC-Menüs nicht offscreen gerendert.
- Kein Release. Altes Rea-Sixty mit neuem ORC: Tasten auf Werk, Bankart 15 fällt beim Laden weg (Parser-Grenze).

## MoreMe: Performer und Studio Control (Frank: ins selbe Release)
Plan: `docs/moreme-performers-plan.md`, alle Stufen gebaut.
- `orc/Performers.*`: Modell, JSON, Desk-Befehle, `deskAllowed`. Datei `moreme-performers.json`, schreibt nur der Server-Thread. 
- `orc/Phones.*`: Mixe `perf:<link>` neben `phone<N>`, Quellen pro Mix (`sourcesOf_`), serverseitige Sperren (fremder Mix, fremde Quelle, Master, EQ/Dyn/FX nach Stufe), Strip-Seite mit Kurve aus `rmeu::eqModel` + `uf1eq::bandDb`.
- `orc/moreme/desk.html`: Studio Control, QR-Encoder in JS (dekodiert mit CIDetector, Versionen 2 bis 10). Vorschau = die echte Handyseite im Frame.
- `orc/moreme/moreme.html`: Link `/p/<id>`, Streifen im Hochformat über die vorhandene Ansicht, dB-Skala, EQ/DYN/FX am Streifen, Kopfzeile, Hell/Dunkel, Wisch-Hinweis, Doppeltipp, Meine Fader, EQ- und Dynamics-Seite.
- `orc/OrcDeskWindow.mm`: eigenes Fenster mit WKWebView, ATS `NSAllowsLocalNetworking`. `/desk` nur 127.0.0.1, ohne PIN (Frank: nur am Mac, nie Tablet).
- Abweichung vom Plan: Doppeltipp nicht auf dem Master (0 dB aus dem Nichts auf den Ohren).

Geprüft: alle ctests, Mixer-Sperren als Test, Demo mit echtem Server und falschem TotalMix auf Systemports im Browser (Desk, Vorschau, EQ, Dyn, Streifen, Querformat, Hell, Meine Fader), WKWebView offscreen mit denselben ATS-Einstellungen.
Nicht geprüft: am echten TotalMix und Handy; das ORC-Fenster und der Reiter im laufenden ORC.

## Aus Franks Test
- UF8-Meter: L = R, beide Balken nahmen den linken Kanal. Regel des UF1 übernommen.
- UF8 und UF1 zeigen je ihre eigene Soft-Key-Bank (`uf8ScBank_`, `g_sideCarBankUf8`), ein Speicher.
- UF8 als Side-Car in Rea-Sixty (TotalMix, Hue, DynaMount): Quick 1-3 und Layer 1-3 ohne Modifier sind tot (`uf8side::deadInSideCar`), sie verstellten die Bank eines unsichtbaren Modus und meldeten das im Banner. Shift + Layer bleibt, das ist der Weg zwischen den Modi.
