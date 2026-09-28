# Session 28.09.2026: Tasten haben nur noch Press und Toggle

Frank: „deine toggle option für shift funktioniert überhaupt nicht", „wieso haben wir
überhaupt mehr optionen als einfach press und toggle?", „mach keine fehler".

## Was vorher war (gegen die echte Engine gemessen)

| Behavior | REAPER-Aktion | Makro / MIDI | Built-in |
|---|---|---|---|
| Momentary „fire on press" | beim Drücken | beim Drücken | Handler bekommt beide Flanken |
| Toggle „flip on each press" | beim Drücken, wie Momentary | beim Drücken | dito |
| Hold „state mirrors button" | Drücken UND Loslassen | zweimal | dito, `firing` auch beim Loslassen |

Modifier hatten dazu ein zweites Menü „Mode“ (Momentary „held = active“ / Toggle), und nur
das wirkte. Franks FINE mit Behavior Toggle + Mode Momentary hielt Shift (Messung
1010 statt 1100).

## Neu

- **Zwei Wahlmöglichkeiten:** *Toggle* (jeder Druck schaltet) und *Press* (an, solange
  gedrückt). Gespeichert wird weiter das alte Feld: Press = `hold`, Toggle = `toggle`,
  ein vorhandenes `momentary` bleibt (anderes Timing beim langen Druck).
- **Die Wahl erscheint nur, wo sie etwas bewirkt** (`offersPressChoice`): ein Built-in,
  das umschaltet, ein Modifier, eine REAPER-Aktion mit Ein/Aus-Zustand, oder eine Taste,
  die schon auf Press steht.
- **`BuiltinKind`** pro Built-in: Once / Switch / Select / Edges. Nicht eingetragen =
  Auto: mit Zustand Select, ohne Zustand Once, also nie eine Wahl, die es nicht halten
  kann. Switch-Liste (88 Namen, jeder Handler gelesen) am Ende von
  `registerBindingHandlers`, RME-Tasten in `RmeBuiltins.cpp`, Modifier in `Bindings.cpp`.
  Unbekannte Namen landen im Log (`[bindings] press kind set for unknown builtin`).
- **Engine, eine Funktion `firingFor_`** für alle drei Dispatch-Wege (vorher dreimal
  derselbe switch): Loslassen feuert nur bei Hold und nur, wo es etwas bedeutet (Switch,
  Edges). Einmal-Aktionen und Auswahlen feuern nicht mehr doppelt. REAPER-Schritte bleiben
  wie bisher: ob eine REAPER-Aktion einen Zustand hat, darf der Geräte-Thread REAPER
  nicht fragen.
- **Modifier:** Press/Toggle ist ihr `param`; das Mode-Menü ist weg, das Behavior-Menü
  schreibt beides. Beim Laden setzt `alignModifierBehavior_` das Behavior nach dem param
  (an beiden Ladewegen, nach allen Migrationen). Keine Taste tut danach etwas anderes.
- Handbuch an sechs Stellen nachgezogen.

## Geprüft

- `test_press_mode`: jede Gruppe mit allen drei gespeicherten Behaviors, Modifier-Tabelle,
  Press/Toggle-Logik, wo die Wahl erscheint, Migration beim Laden. Fällt um, wenn Hold
  wieder immer feuert (2 Fehler) und ohne Migration (2 Fehler).
- 88 Switch-Namen statisch gegen alle Registrierungen: alle vorhanden.
- Franks `bindings.json`: nur drei `hold` (Jog-Inhalt ziehen x2, UF1-SHIFT), alle Edges.
- RME-Tasten: `Manager::send` schreibt sofort ins Abbild (`localEcho`), darum schaltet
  Press beim Loslassen sauber zurück.

## Offen / ungesehen

- Am Gerät und im Settings-Fenster nicht gesehen. Log beim nächsten REAPER-Start auf
  `press kind set for unknown builtin` prüfen.
- Franks FINE (Behavior Toggle, param 0) zeigt nach dem Laden „Press“, weil es hält. Toggle
  wählen, dann schaltet es.
- Umschalter über Merkflag (Learn-HUD, Mixer, Schlafen, Plug-in-Fenster, Bypass, Offline,
  FX-Kette): Drücken und Loslassen im selben Takt zählen als ein Umschalten.
