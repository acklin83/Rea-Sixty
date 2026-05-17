# Plan: Instance / FX / Strip Cycle Cleanup

Status: design draft, Code-Review 2026-05-16. Nichts gebaut. Ergebnis aus
einem manuellen Walk-through von `applyInstanceCycle_` /
`applyFxCycle_` / `syncInstanceFromFxIdx_` und den per-channel
Cycle-Handlern (`StripInstanceDelta` / `StripInstanceCycleDelta`) in
[main.cpp](../extension/src/main.cpp).

Querverweis: [docs/concepts.md](concepts.md) und
[docs/instances-fx-handbuch.md](instances-fx-handbuch.md) für die
User-facing Definitionen von FX vs. Instance.

## Terminologie (gilt für dieses Doc)

Das Wort "Strip" ist im Code dreifach belegt. In diesem Plan sauber
getrennt:

- **UF8-Kanal** (oder **Hardware-Kanal**, **Kanal N**, N ∈ [0,8)) — die
  physische Spalte am UF8 mit Fader + V-Pot + SEL/CUT/SOLO + Scribble
  Strip. Im Code als `strip` (uint8_t), `s`, `g_stripInstanceAccum[s]`,
  `PendingInput::strip` Field. Existiert immer; gehört zur Hardware.

- **Kanal-Track** — der `MediaTrack*` der gerade auf Kanal N angezeigt
  wird (resolviert via `g_bankOffset + visibleTrackAt(slot)`). Wechselt
  bei Bank-Scroll. `g_stripInstanceFxIdx[tr]` ist auf diesen Track
  gekeyt — der Name "strip" suggeriert irreführend etwas Kanal-Lokales,
  obwohl es per-Track ist.

- **Strip** (gross, SSL-Sinn) — eine CS-Instance, deren *Fader Level*
  Param auf den UF8-Fader von Kanal N gemappt ist. Existiert nur wenn:
  1. SSL Strip Mode (`g_pluginFaderMode`) aktiv ist, UND
  2. Der Kanal-Track eine CS-Instance trägt, UND
  3. Die CS-Instance einen Fader-Level-Slot exponiert (CS 2 / 4K B / E
     / G / Link / User-CS).

  Wenn nur (1) und (2) zutreffen ohne (3), ist der UF8-Fader auf
  REAPER-Track-Volume gemappt — kein Strip. Wenn (1) nicht zutrifft, ist
  der Kanal eine reine REAPER-Mixerspalte — kein Strip.

  Analog gibt's einen **UF8-Plugin-Mode-Strip**: alle 8 UF8-V-Pots
  zusammen treiben Params einer User-Instance mit `uf8Mode==true`. Das
  ist ein Strip-Konzept über alle 8 Kanäle hinweg, kein per-Kanal-Strip.

Faustregel im Code-Review:
- Wenn der Code `strip` schreibt und meint Index 0–7 → **UF8-Kanal**.
- Wenn der Code "the strip's track" sagt → **Kanal-Track**.
- Wenn die Doku oder UI von SSL Strip Mode redet → **Strip** im
  strikten Sinn.

## Wie das System heute zusammenhängt (Kurzfassung)

Drei Cursor pro Kanal-Track, drei Cycle-Pfade, drei GUI-Owner-Slots.

**Cursor:**
- `g_stripInstanceFxIdx[MediaTrack*]` — der **FX-Cursor**, Index in
  `TrackFX_GetCount`. Pointer-gekeyt.
  [main.cpp:1505](../extension/src/main.cpp:1505)
- `g_csInstanceMap[guid]`, `g_bcInstanceMap[guid]`,
  `g_uf8OnlyInstanceMap[guid]` — die **Instance-Cursor** je Domain.
  GUID-gekeyt.
  [UC1PluginMap.cpp:617-619](../extension/src/UC1PluginMap.cpp:617)

**Cycle-Pfade:**
- Focused-Track-Scope (via UF8 Channel Encoder, UC1 Encoder 2,
  Builtin-Aktionen): `applyInstanceCycle_`
  ([main.cpp:1753](../extension/src/main.cpp:1753)),
  `applyFxCycle_` ([main.cpp:1977](../extension/src/main.cpp:1977)).
- Per-Kanal-Scope (V-Pot-Rotation auf Kanal N):
  `PendingInput::StripInstanceDelta`
  ([main.cpp:3445](../extension/src/main.cpp:3445)),
  `PendingInput::StripInstanceCycleDelta`
  ([main.cpp:3530](../extension/src/main.cpp:3530)),
  `PendingInput::StripInstanceOpen`
  ([main.cpp:3512](../extension/src/main.cpp:3512)).
- Helper: `syncInstanceFromFxIdx_`
  ([main.cpp:1912](../extension/src/main.cpp:1912)) — promoted einen
  FX-Cursor-Landpunkt zu einer Instance-Auswahl.

**GUI-Owner:**
- `g_csGuiShownTr/Fx` — SSL Strip Mode (with GUI)
- `g_uf8GuiShownTr/Fx` — UF8 Plugin Mode (with GUI)
- `g_instanceGuiShownTr/Fx` (+ `g_instanceGuiOwnerStrip`) — V-Pot-Push
  aus Selection-Mode Instance / InstanceCycle
- `g_focusedGuiShownTr/Fx` — `show_focused_plugin_gui` und
  UC1 Encoder-2-Push

Alle vier werden zentral im Drain bei
`g_pluginGuiSyncRequest.exchange(false)`
([main.cpp:9118](../extension/src/main.cpp:9118)) gehandhabt.

---

## Findings, sortiert nach Priorität

### P0 — User-visible Bugs

#### 1. V-Pot Cycle zieht SSL Strip / UF8 Plugin Mode GUI-Fenster nicht mit

**Wo:**
[main.cpp:3445-3511](../extension/src/main.cpp:3445)
(`StripInstanceDelta`),
[main.cpp:3530-3625](../extension/src/main.cpp:3530)
(`StripInstanceCycleDelta`).

**Symptom:** SSL Strip Mode (with GUI) ist an — d.h. der fokussierte
Kanal trägt einen **Strip** im strikten Sinn (CS-Instance + Fader Level
auf UF8-Fader). Townhouse CS-Fenster ist offen. User dreht V-Pot auf
demselben Kanal in `selection_mode_instance_cycle`. → `csInstanceIndex`
springt korrekt auf 4K G, der Strip-Verbund (Fader, V-Pot-Labels)
wandert mit, aber das Floating-Fenster bleibt auf Townhouse. Gleiches
Verhalten in UF8 Plugin Mode (with GUI).

**Vergleich:** `applyInstanceCycle_` hat das `triggerFollowSync`-Lambda
([main.cpp:1834-1841](../extension/src/main.cpp:1834)), das
`g_pluginGuiSyncRequest` setzt, wenn `g_pluginFaderMode +
g_pluginFaderModeWithGui` oder `g_uf8PluginMode +
g_uf8PluginModeWithGui` aktiv ist. Die per-Kanal-Pfade setzen das Flag
nur wenn `g_instanceGuiOwnerStrip == s`
([main.cpp:3505](../extension/src/main.cpp:3505),
[3621](../extension/src/main.cpp:3621)) — das deckt nur den V-Pot-Push-
GUI-Owner ab, nicht die Plugin-Mode-Master-GUIs (= die Fenster die zum
Strip selbst gehören).

**Fix:** `triggerFollowSync`-Lambda aus
`applyInstanceCycle_` extrahieren (eigene Funktion oder File-Local
Lambda) und in beiden per-Kanal-Handlern aufrufen — aber nur wenn
**der gedrehte Kanal aktuell einen Strip im strikten Sinn trägt**, d.h.
`tr == focusedTrack` (das ist die einzige Stelle wo Plugin-Mode-GUIs
gemerkt werden). Rotationen auf non-focused Kanälen dürfen die globale
Plugin-Mode-GUI nicht kapern.

Pseudo:
```cpp
const bool isFocusedChannel = (focusedTr == tr);
if (isFocusedChannel) triggerPluginModeFollowSync_();
```

Implizit deckt die Gate-Logik in `triggerFollowSync` (mit-GUI-Flag muss
gesetzt sein) den Strip-Bedingung-Teil mit ab: ohne SSL Strip Mode + with
GUI gibt's keinen CS-GUI-Owner, also nichts zu syncen. Deshalb ist die
einzige zusätzlich nötige Bedingung "Kanal == focused".

**Repro:** SSL Strip Mode with GUI an, Track mit zwei CS-Instanzen
(z.B. Townhouse + 4K G). V-Pot auf dem fokussierten Kanal drehen →
Fenster sollte mit auf 4K G wechseln. Heute bleibt es auf Townhouse.

**Risiko:** niedrig. Die Sync-Routine bei
[main.cpp:9118](../extension/src/main.cpp:9118) ist idempotent.

---

#### 2. `syncInstanceFromFxIdx_` und `StripInstanceCycleDelta` kapern BC-Anchor unbedingt

**Wo:**
[main.cpp:1912-1962](../extension/src/main.cpp:1912)
(`syncInstanceFromFxIdx_`, default-Argument `setBcAnchor = true`),
Aufruf in `StripInstanceDelta`
([main.cpp:3479-3481](../extension/src/main.cpp:3479)),
und direkt in `StripInstanceCycleDelta`
([main.cpp:3601-3603](../extension/src/main.cpp:3601)).

**Symptom:** V-Pot FX Cycle oder Instance Cycle auf einem
**nicht-fokussierten** UF8-Kanal (z.B. Kanal 5), der zufällig auf einem
BC-Plug-in landet, zieht den UC1 BC-Anchor auf den Track von Kanal 5.
Die BC-Encoder-Section auf UC1 zeigt jetzt einen Track, den der User
gar nicht im Fokus hatte.

**Widerspruch:** Der Kommentar bei
[main.cpp:3593-3597](../extension/src/main.cpp:3593) sagt explizit
*"focused.domain shifts only when the strip belongs to the currently
focused track — otherwise a non-focused-strip rotation would silently
hijack UC1 / SSL Strip Mode focus."* Der `setFocusedDomain`-Schalter
respektiert die Regel; der hartcodierte `setBcAnchor=true`-Default
bricht sie still.

**Fix:**
1. `syncInstanceFromFxIdx_(tr, fxIdx, setFocusedDomain, setBcAnchor)` —
   beide Schalter parallel führen, statt `setBcAnchor` als default
   anzunehmen.
2. Caller in `StripInstanceDelta` ([3479](../extension/src/main.cpp:3479)):
   `syncInstanceFromFxIdx_(tr, next, isFocusedChannel, isFocusedChannel)`.
3. Caller in `StripInstanceCycleDelta`
   ([3601-3603](../extension/src/main.cpp:3601)): die explizite
   `setBcAnchorTrack(tr)` ebenfalls hinter `if (isFocusedChannel)`
   gaten.
4. Focused-Scope-Pfad in `applyInstanceCycle_`
   ([main.cpp:1848-1852](../extension/src/main.cpp:1848)) bleibt
   unverändert — der ist per Definition focused.

**Repro:** Zwei Tracks T_A (CS) und T_B (BC) sichtbar auf Kanälen 0 und
3. T_A ist fokussiert. UC1 BC-Anchor sitzt auf T_B. User dreht V-Pot
auf Kanal 0 (T_A) in FX Cycle → erwartetes Verhalten: BC-Anchor bleibt
T_B. Heute: wenn der FX-Cycle auf einer (hypothetischen) BC-Instance
auf T_A landet, springt der BC-Anchor auf T_A. Schwieriger zu
reproduzieren als #1 weil es einen BC auf dem CS-Track braucht.

**Risiko:** mittel. Es gibt evtl. einen User-Workflow der die
unbedingte BC-Anchor-Pin als Feature erlebt ("Cycle landet auf BC →
UC1 zeigt sofort dieses BC"). Vor dem Fix kurz mit Frank klären.

---

### P1 — UX-Inkonsistenzen

#### 3. `applyInstanceCycle_` Carousel zeigt am Ring-Ende leere Nachbarn

**Wo:** [main.cpp:1887-1895](../extension/src/main.cpp:1887).

**Symptom:** Bei `nextK == 0` ist `hitLabel(nextK - 1)` = `hitLabel(-1)`
→ leer. Bei `nextK == size - 1` ist `hitLabel(size)` → leer. Der Cycle
wrappt modular, der Carousel zeigt aber "(blank) | aktuell | nächster"
am Anfang und "vorheriger | aktuell | (blank)" am Ende.

**Vergleich:** `applyFxCycle_`
([main.cpp:2003-2009](../extension/src/main.cpp:2003)) macht das
korrekt mit `((nextIdx - 1 + n) % n)`.

**Fix:** Same modular wrap in `applyInstanceCycle_`:
```cpp
const int sz = static_cast<int>(hits.size());
const int prevK = (nextK - 1 + sz) % sz;
const int nxtK  = (nextK + 1) % sz;
g_uc1_surface->showInstanceCarousel(
    hitLabel(prevK), hitLabel(nextK), hitLabel(nxtK), header);
```

**Risiko:** trivial.

---

#### 4. Per-Kanal Cycle-Handler haben keine Carousel-Anzeige

**Wo:** `StripInstanceDelta`
([3445-3511](../extension/src/main.cpp:3445)) und
`StripInstanceCycleDelta`
([3530-3625](../extension/src/main.cpp:3530)) rufen
`showInstanceCarousel` nicht auf.

**Symptom:** Die identische Aktion (FX bzw. Instance Cycle) zeigt auf
dem UC1 LCD prev/curr/next wenn via UF8 Channel-Encoder oder UC1
Encoder 2 ausgelöst, ist aber stumm wenn via V-Pot auf einem UF8-Kanal
gedreht.

**Entscheidung (Frank 2026-05-16):** Carousel nur dann feuern wenn der
gedrehte Kanal aktuell ein **Strip** im strikten Sinn ist —
d.h. `tr == focusedTrack` (das ist die einzige Stelle wo UC1 LCD-State
gemerkt wird). Sonst würde eine V-Pot-Rotation auf Kanal 5 das UC1
Display zu Kanal-5-Inhalt umschalten, während der User UC1 auf den
fokussierten Track erwartet. Konsistent mit dem Gate aus #1.

**Fix:** Carousel-Build aus `applyInstanceCycle_` /
`applyFxCycle_` in eine kleine Helper-Funktion auslagern:
```cpp
void showCycleCarousel_(MediaTrack* tr, int curIdx,
                       const std::vector<int>& ringFxIdx);
```
und aus allen vier Cycle-Pfaden aufrufen, in den per-Kanal-Pfaden
gegated auf `isFocusedChannel`.

**Risiko:** niedrig wenn gegated. Achtung Race mit anderen UC1-Overlay-
Konsumenten (BC-Track-Scroll-Banner u.ä.) — wird durch das focused-
only-Gate aber praktisch entschärft.

---

### P2 — Design-Konsistenz / latent

#### 5. Anker-Mismatch: `applyInstanceCycle_` vs `StripInstanceCycleDelta`

**Wo:**
- `applyInstanceCycle_`
  ([main.cpp:1812-1817](../extension/src/main.cpp:1812)) findet `curK`
  über `(hits[k].dom == curDom && hits[k].instIdx == curInstIdx)`.
- `StripInstanceCycleDelta`
  ([main.cpp:3582-3586](../extension/src/main.cpp:3582)) findet `curK`
  über `hits[k].fxIdx == curFx`.

**Symptom:** Im Normalfall konsistent, weil beide Pfade am Ende
denselben Cursor schreiben (`g_stripInstanceFxIdx[tr] = target.fxIdx`
und `setCsInstanceIndex/setBcInstanceIndex/setUf8OnlyInstanceIndex`).
Bei FX-Re-Order in REAPER (Drag-Drop) bleiben die Domain-Cursor stabil
(CS-Position 1 zeigt jetzt auf einen anderen FX-Index), der FX-Cursor
veraltet. Die beiden Pfade starten dann aus unterschiedlichen
Positionen im Ring.

**Fix:** Beide Pfade auf denselben Anker — `(dom, instIdx)` ist
robuster gegen Re-Ordering, weil die Domain-Cursor die User-Intention
("zweite CS-Instance") direkt encodieren. Der FX-Cursor sollte nach
einem Re-Order via `syncInstanceFromFxIdx_` neu aufgesetzt werden,
oder onTimer einmal pro Track gegen Domain-Cursor abgeglichen.

**Risiko:** mittel. Berührt Cycle-Anker-Logik in zwei Pfaden, braucht
sorgfältigen Test.

---

#### 6. `g_stripInstanceFxIdx` ist Pointer-gekeyt, alle anderen Cursor GUID-gekeyt

**Wo:**
- [main.cpp:1505](../extension/src/main.cpp:1505):
  `std::unordered_map<MediaTrack*, int>`.
- [UC1PluginMap.cpp:617-619](../extension/src/UC1PluginMap.cpp:617):
  `std::unordered_map<std::string, int>` (GUID).

**Symptom:** Nach Project-Reload sind die drei Domain-Cursor stabil
(GUID überlebt), der FX-Cursor ist auf nullptr-Tracks geworfen und
fällt auf 0 zurück. Cycle-Verhalten direkt nach Reload startet bei
FX 0 obwohl der CS/BC-Cursor noch sinnvoll wäre. Kein Crash
(`stripInstanceActiveFx_` klemmt bei
[main.cpp:1517-1518](../extension/src/main.cpp:1517) auf gültigen
Range), aber inkonsistente Persistenz.

**Zweites Symptom:** REAPER kann `MediaTrack*` recyclen wenn ein Track
gelöscht und ein neuer angelegt wird. Im Worst Case erbt der neue
Track den alten FX-Cursor.

**Fix:** `g_stripInstanceFxIdx` auf GUID-Map umstellen. Same Pattern
wie `g_csInstanceMap` — Helper `trackGuid_` ist bereits in
`UC1PluginMap.cpp` etabliert, könnte in einen geteilten Header (oder
nach `PluginMap`) wandern.

**Risiko:** niedrig — Lookup-Site ist `stripInstanceActiveFx_`, Write-
Sites sind ~5 Stellen. Performance OK (Cycle-Aktionen sind sparse).

---

#### 7. `applyInstanceCycle_` curDom-Fallback ist asymmetrisch

**Wo:** [main.cpp:1803-1807](../extension/src/main.cpp:1803).

**Symptom:** Fängt nur `curDom == None && uf8OnlyCount == 0` ab und
fällt auf CS/BC zurück. Aber `curDom == ChannelStrip` auf einem Track
ohne CS-Hits (nur BC + UF8-only) wird nicht gefangen — `curK` bleibt 0,
Cycle startet bei `hits[0]`. Ein Detent geht "verloren" weil der User
gefühlt von keiner aktuellen Instance gestartet ist.

**Fix:** Generische Fallback-Logik. Wenn kein Hit mit `curDom` im Ring
existiert, fallback auf die erste vorhandene Domain (CS → BC → None).

```cpp
auto hasDom = [&](uf8::Domain d) {
    for (const auto& h : hits) if (h.dom == d) return true;
    return false;
};
if (!hasDom(curDom)) {
    curDom = hasDom(uf8::Domain::ChannelStrip) ? uf8::Domain::ChannelStrip
           : hasDom(uf8::Domain::BusComp)      ? uf8::Domain::BusComp
           :                                     uf8::Domain::None;
}
```

**Risiko:** trivial. Macht die existierende Spezial-Logik bei 1803
überflüssig.

---

### P3 — Kosmetik / Cleanup

#### 8. Stale Kommentar in `applyInstanceCycle_`

**Wo:** [main.cpp:1786-1790](../extension/src/main.cpp:1786).

Kommentar behauptet *"UF8-only user maps don't surface via
lookupPluginMapByName (their synthesised PluginMap carries Domain::None
and no slots)."* Tatsächlich liefert `lookupPluginMapByName` (via
`user_plugins::lookupByName` ([UserPluginCatalog.cpp:859](../extension/src/UserPluginCatalog.cpp:859)))
sehr wohl UF8-only-Maps zurück — sie haben nur `domain=None`. Der
Fall-through funktioniert weil die CS/BC-Checks Domain::None ablehnen,
nicht weil `pm` nullptr ist.

**Fix:** Kommentar umschreiben. Code unverändert.

---

#### 9. Toter Code: `uc1::cycleInstance`

**Wo:** [UC1PluginMap.cpp:877](../extension/src/UC1PluginMap.cpp:877),
deklariert in [UC1PluginMap.h:145](../extension/src/UC1PluginMap.h:145).

Leftover aus der Shift+Channel-Encoder-Era; die Cycle-Logik läuft
heute über `applyInstanceCycle_` in main.cpp. Cross-grep zeigt
keine Aufrufstelle.

**Fix:** Funktion + Header-Deklaration löschen.

**Risiko:** trivial.

---

## Was nicht in diesem Plan ist (aber im Walk-through auftauchte)

- **Zwei parallele Lookup-Pfade** (`uf8::lookupPluginMapByName` vs
  `uc1::lookupBindingsByName` + `isBusCompBinding`) — by design.
  UF8-Pfad braucht die SSL-360-Link-Slot-Tabellen, UC1-Pfad braucht die
  physische Knob/Button-Map. Beide ziehen vom selben User-Catalog
  (`user_plugins`), Konsistenz ist über die `domain`-Felder
  sichergestellt. Kein Bug.

- **`stripInstanceActiveFx_` clamping ohne Write-Back** (bei
  [1517-1518](../extension/src/main.cpp:1517)). Map kann stale
  out-of-bounds Werte halten; nächster Read klemmt wieder. Kein
  funktionales Problem; jeder Cycle schreibt sowieso einen frischen
  Wert zurück.

---

## Empfohlene Reihenfolge

1. **#1** (V-Pot Cycle → Plugin Mode GUI-Follow). User-visible, kleiner
   Diff, hoher Wirkungsgrad.
2. **#2** (BC-Anchor-Hijack). User-visible, kleiner Diff. Aber: kurz
   mit Frank checken ob das aktuelle Verhalten irgendwo bewusst
   ausgenutzt wird, bevor wir's gaten.
3. **#3** (Carousel-Wrap). Trivial, mit #1 zusammen pushen.
4. **#4** (Carousel auf per-Kanal, focused-only gegated — Frank
   2026-05-16 entschieden).
5. **#5** (Anker-Mismatch) + **#6** (FX-Cursor → GUID) zusammen — beide
   touchen `g_stripInstanceFxIdx`-Storage und können in einem
   Refactor-Commit kombiniert werden.
6. **#7, #8, #9** als Cleanup-Sammelpush.

Jedes Stück eigener Commit, damit Bisects bei Regressionen klar
laufen.

---

## Test-Matrix (manuell, am Mac mit UF8 + UC1 live)

Für jeden Bugfix dieselbe Grundlage:

- Track T1 mit CS 2 + 4K G + Townhouse CS (3 CS-Instanzen)
- Track T2 mit BC 2 + bx_townhouse (1 CS + 1 BC, weil bx Combo ist) —
  oder ein klar getrenntes BC-Plug-in falls vorhanden
- Track T3 mit zwei UF8-only-Plug-ins (z.B. zwei FF-EQs gelernt mit
  `uf8Mode`)

Pro Fix die relevanten Cycle-Pfade durchklicken:
- UC1 Encoder 2 (Plain = `fx_cycle`, Shift = `instance_cycle`)
- UF8 Channel Encoder (Plain = `encoder_mode_dispatch` mit Instance-
  Mode aktiv, Shift = `instance_cycle`)
- V-Pot auf fokussiertem Kanal (SelectionMode `instance` und
  `instance_cycle`)
- V-Pot auf non-fokussiertem Kanal (dito)
- V-Pot-Push aus Cycle-Modi (öffnet/schliesst GUI)

Was beobachten: Domain-Cursor (`csInstanceIndex` etc.), FX-Cursor
(`g_stripInstanceFxIdx`), UC1 LCD-Anzeige, BC-Anchor, alle vier
GUI-Owner-Slots.
