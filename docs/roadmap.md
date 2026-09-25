# Roadmap

Angefangene Gedanken, die noch keinen eigenen Plan haben. Was hier steht, ist
aufgenommen, nicht beschlossen, und **nichts davon wird gebaut, bevor Frank es
einzeln freigibt**.

Sobald ein Punkt gross genug ist, bekommt er eine eigene Datei und hier bleibt
nur eine Zeile mit dem Verweis. Alles hier ist **nach v0.6.0** (Grenze:
`87edabf`, siehe `.local-docs/bench-v0.6.0.md`).

---

## 1. RME-Pegel auf das Fader-Display, und TotalMix-Farben

Frank, 20.09.2026: *"rme OSC kann Pegel! Auf Fader-Display anzeigen und
totalmix farben als option in settings, REC mode?"*

Die Pegel stimmen, das steht in RMEs eigener Tabelle und ist keine Vermutung:

```
/level/in/<n>    f   Peak level [dB], nur sendend, nur bei Aenderung
/level/pb/<n>
/level/out/<n>
```

### 1.1 Der Pegel

Die Fader-Seite des UF1 hat **einen** Schreiber fuer ihren kleinen Meter:
`uf1ChannelMeterBytes_(tr, lvL, lvR, compByte, gateByte)`, und der Wert geht
als `{lvL, lvR, 0, 0}` auf `0x0009` (`main.cpp:33838`). Eine zweite Quelle ist
damit **ein Tausch in einer Funktion**, nicht ein zweiter Meterpfad -- dieselbe
Form wie beim EQ-Graph, wo der Sammler getauscht wird und der Renderer bleibt.

Der UF8 hat denselben Gedanken acht Mal (`pushVuMeter`), also faellt er
hinterher mit ab.

### 1.2 Warum REC-Mode die richtige Heimat ist

Frank stellt es als Frage, und die Antwort faellt aus dem Code: **im REC-Mode
ist die Zuordnung Spur zu Hardware-Eingang schon da.** `I_RECINPUT` sagt,
welcher Eingang die Spur speist, und genau dieser Index ist `<n>` in
`/level/in/<n>`. Im Side-Car gibt es diese Bruecke nicht, dort IST die Flaeche
TotalMix.

Also: REC-Mode zeigt den Pegel, den das Interface sieht, neben dem Pegel, den
REAPER sieht. Das ist beim Einpegeln die interessante Zahl, weil sie **vor**
allem liegt, was REAPER damit macht.

### 1.3 Die Farben, und die eine Huerde

`/input/<n>/color` ist **nur sendend** und liefert einen **Palettenindex**,
kein RGB. Auf Franks Rig kamen 0 bis 8 vor, und **0 heisst versteckt** (das ist
gleichzeitig der Filter, welche Kanaele diese Fernbedienung ueberhaupt sieht).

Unser Farbbalken nimmt auch einen Index, aber unseren
(`uf8::quantize(rgb)` in `Palette.cpp`). Es fehlt also **die Tabelle Index zu
RGB von TotalMix**. Die steht in keiner Spezifikation; sie muss einmal
abgelesen werden.

⛔ Und zwar gemessen, nicht per Zuruf: eine Tabelle "Index 5 ist welches
Gruen" gehoert aus einem Screenshot von TotalMix oder aus den Ressourcen der
App, wo sich der Hue auszaehlen laesst.

### 1.4 Was zuerst gemessen werden muss

Der Haken **"Send Peak Level"** steht fuer Remote 1 auf aus, also kommt heute
kein einziges `/level/...`. Wie oft es dann kommt, weiss niemand, und davon
haengt ab, ob der Strom direkt in den 24-Hz-Zyklus darf oder gepuffert werden
muss. Braucht eine laufende UFX+.

---

## 2. ✅ GEBAUT 22.09.2026 (`390b76e`, auf 0.6 als `f728ce1`)

Das Tor ist geteilt: `recInputStepActive_()` fragt nur nach REC / REC + MON, die
drei Shift-Wege (UF8, UC1, UF1) haengen daran, der Eingangsname ebenfalls. Gain,
48V, Pad, Phase und die gespiegelten LEDs behalten `recRmeActive_()`. Franks
Entscheidung zur Kollision mit Fine: **die Vorgabe bleibt an** ("shift auf v-pot
ist dort ok, sind eh in eigenem mode"). Der Rest dieses Abschnitts ist die
Vorgeschichte.

## 2. REC-Mode soll REAPERs Eingaenge auch ohne RME steppen

Frank, 20.09.2026: *"rec mode soll auch reaper interne Inputs steppen koennen
wie Rme Mode"*.

Das ist kleiner, als es klingt: **die Funktion existiert und tut schon genau
das.** `recRmeStepInputChannel_` (`main.cpp:17492`) liest `I_RECINPUT`, laesst
MIDI (Bit 4096) und Mehrkanal (Bit 2048) in Ruhe und schreibt den Kanal
zurueck. Darin kommt **kein OSC und keine RME-Hardware vor**, das ist reines
REAPER.

Nur haengt sie hinter dem falschen Tor:

```cpp
inline bool recRmeActive_()
{
    if (!g_recRmeEnabled.load()) return false;          // ← die RME-Integration
    const auto m = g_selectionMode.load();
    return m == SelectionMode::Rec || m == SelectionMode::RecMon;
}
```

Wer keine RME hat, bekommt das Eingangs-Steppen also nicht, obwohl es ihn
nichts angeht. Die Aenderung ist, das Tor zu **teilen**: der Eingangsschritt
haengt an REC/RecMon allein, Gain, 48V, Pad und Phase bleiben hinter
`g_recRmeEnabled`, weil die ohne Interface wirklich nichts tun.

⛔ Aufpassen: `recRmeActive_()` hat mehrere Leser (V-Pot-Rotation, Tasten-LEDs,
Beschriftung). Vor der Aenderung greppen, wer sonst noch daran haengt -- sonst
faellt beim Teilen eine Lampe mit, die niemand gemeint hat.

**Die Geste bleibt, Frank 20.09.: Shift plus V-Pot.** Also genau die, die es
heute schon ist, nur ohne die RME-Bedingung davor. Sie existiert pro Flaeche
und jeweils mit eigenem Schalter: `g_recUf1ShiftInputCh` (UF1),
`recVpotShiftInputCh` (UF8), `recUc1Enc2ShiftInputCh` (UC1).

⛔ **Shift hat auf einem V-Pot aber schon einen Job.** `shiftFineActive_()`
macht daraus Fine Mode, wenn die Einstellung "Shift activates Fine mode" an ist.
In REC + RME kollidiert das heute bereits und Frank lebt damit; faellt das
RME-Tor weg, breitet sich die Kollision auf den **reinen** REC-Mode aus.

Und weil `g_recUf1ShiftInputCh` auf **`true`** vorbelegt ist, waere das kein
Angebot, sondern eine Aenderung an allen bestehenden Setups: wer im REC-Mode
bisher mit Shift fein gepannt hat, steppt danach Eingaenge.

Die eine Entscheidung dazu: Vorgabe auf `true` lassen (gleich wie im RME-Mode,
aber es aendert sich etwas ungefragt) oder fuer den RME-losen Fall auf `false`
setzen (nichts aendert sich, wer es will, holt es sich). Ich waere fuer
`false`, weil die Fine-Geste aelter ist.


---

## 3. Rea-Sixty wird geduldig beim Oeffnen der Geraete

Faellt als Voraussetzung aus dem Standalone heraus, ist aber **fuer sich
genommen ein Fix**, unabhaengig davon, ob das Standalone je kommt.

`openUf1BringUp_` versucht `open()` genau einmal. Scheitert es, wird geloggt
und `g_uf1_dev.reset()` aufgerufen — danach existiert das Objekt nicht mehr,
und die 5-Sekunden-Wiederhol-Schleife sieht nur Geraete an, die existieren und
`needsReopen()` melden. Ein Wiederverbinden-Knopf existiert nicht.

Drei Faelle sehen deshalb heute wie ein defektes Geraet aus, und alle drei sind
haeufig:

* REAPER gestartet, bevor der UF1 angesteckt war
* SSL 360 hielt das Geraet beim Start noch
* (kuenftig) das Standalone hielt es

**Die Aenderung:** bei gescheitertem Open das Objekt behalten, `needsReopen`
setzen, die bestehende Schleife macht den Rest. Dazu die Zeile unter
*Connected devices* den Grund sagen lassen, den `lastError()` schon kennt,
statt nur grau zu bleiben.

⛔ Vorher greppen, wer `g_uf1_dev` auf Nicht-Null prueft: ein Objekt, das jetzt
auch im nicht-offenen Zustand existiert, aendert die Bedeutung jedes solchen
Tests. `isOpen()` ist die richtige Frage, `!= nullptr` war es nur zufaellig.
Gilt gleichermassen fuer UF8 und UC1, die denselben Aufbau haben.


---

## 4. ORC — das UF1-Standalone

**Name entschieden 20.09.2026: ORC, "Open Remote Control"**, gegen RMEs ARC
gestellt. Ohne Geraet im Namen (nicht "UF1-ORC"), damit der UF8 spaeter ein
Feature bleibt und kein zweites Produkt wird, und weil `UF1` SSLs
Modellbezeichnung ist.

**Eigenes CMake-Target im Rea-Sixty-Repo**, kein eigenes Repo — es lebt von
sechs Quellen, die hier liegen, und Duplikate laufen auseinander.

Voller Plan in `docs/uf1-sidecar-rme-feasibility.md`, Abschnitt 12. Kurz:
Menueleisten-App, die den UF1 haelt und TotalMix ueber Global OSC fernsteuert;
spaeter zieht `stoerme`s Handy-Seite ein und der Mixer hat ein Gesicht auf dem
Geraet und eines im Browser.

⛔ Nicht vor dem Side-Car anfangen. Die Reihenfolge steht in Abschnitt 16.
