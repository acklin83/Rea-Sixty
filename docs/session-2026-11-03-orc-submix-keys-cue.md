# Session 03.11.2026: UF8-Submix-Tasten, Submix-Bank, Cue

Auslöser: djp53 (Dave) im RME-Forum, Thread ORC, Posts 10 und 11. Frank: bauen, weil es den UF8 zum besten Live-Controller für TotalMix macht.

## Gebaut
- SEND/PLUG-IN 1-8: je ein Ausgang, `Config::uf8SubmixKeys`, `rme.json` Block `uf8.submixKeys`, ORC-Reiter UF8. Werk = bisherige Belegung. Kein Versions-Bump, alte Dateien behalten das Werk.
- `rmeu::keyOutput`: versteckt, rechte Hälfte, kein Ausgang = dunkel und tot. War ein Fehler: eine versteckte Rolle leuchtete, der Druck landete still auf Main (`effectiveSubmix`). Phones 3/4 hängen bei Frank am Snapshot.
- Bankart `RmeSubmixes = 15`. Ausgang steckt im Slot (`ActionSlot::textParam`, Aktion `rme_submix`, damit der Serialiser den Slot behält; gefeuert wird sie nie). Banken 10.. liegen in orc.json, Rea-Sixty liest sie, darum keine Liste in Rea-Sixty.
- Cue: `/controlroom/cuechan`. Shift + SEL auf Ausgängen, Shift + SEND/PLUG-IN, Shift + Taste der Submix-Bank. Submix bleibt. Gelb, Helligkeit weiter = Submix. SOLO-Lampe auf Ausgängen = Cue. UF1 gleich.
- Shift: ORC UF8 = FINE (0x6F), Tastatur nur beim Druck (`orc/OrcKeyboard.h`), kein Spiegel. Rea-Sixty `shiftHeldAnywhere_()`.
- Handbuch ORC: Abschnitt Cue, Soft-Key-Bänke, UF8, Reiter.

## Gemessen (Frank, Remote 7005/7006)
- `cuechan=8` kommt an, `-1` löscht, beides im nächsten `/sendall` gelesen. Kein Echo an den schreibenden Remote.
- Submix-Auswahl von TotalMix: keine Adresse, auch mit „Submix linked to OSC controller 3“ still. Follow gestrichen (Frank: „ist eh flexibler ohne“).

## Offen
- Die neuen ORC-Menüs nicht offscreen gerendert.
- Kein Release. Altes Rea-Sixty mit neuem ORC: Tasten auf Werk, Bankart 15 fällt beim Laden weg (Parser-Grenze).
