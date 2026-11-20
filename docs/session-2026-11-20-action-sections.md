# Session 2026-11-20: REAPER actions outside Main

## Trigger
Forum: in the user jog modes only Main actions could be picked; a MIDI Editor
Command ID typed by hand fired a different action. Frank: "Reaper hat für
verschiedene Umgebungen spezielle actions. Können wir die alle auswählen?"

## Cause
Every REAPER step was Main: `PromptForAction(..., section 0)` in all four picker
starts, the step stored only the number, and `__reaper_action__` ran it through
`Main_OnCommand`, where a MIDI Editor number is some other action. Names and
toggle states were looked up in Main too.

## Built (`445bcbb4`)
- `ActionStep::section` (Bindings.h), `kActionSection*` and `actionSectionKnown`:
  Main 0, MIDI Editor 32060, MIDI Event List 32061, Crossfade Editor 32065.
  JSON key `section`, written only when not Main; unknown values load as Main.
- Run: `runStep_` sends a non-Main step to the new `Host::runSectionAction`;
  main.cpp queues `PendingInput::SectionAction` (section as a byte code) and runs
  `MIDIEditor_LastFocused_OnCommand(cmd, false/true)` or
  `CrossfadeEditor_OnCommand(cmd)` on the main thread.
- `Host::toggleState(section, cmd)`; main.cpp `actionToggleState_` uses
  `GetToggleCommandStateEx` outside Main. Also the UF1 modifier-slot LED and the
  editor's repeat lock.
- Editor: *Section* menu above the Action ID; changing it clears the action.
  Browse opens the Action List on that section, Load ReaScript registers the
  script there, the resolved name comes from that section.
- User jog mode *One action with direction*: Main only. Locked (grey text) when
  the Turn right binding is outside Main; the dispatch fires nothing for a
  non-Main action in that kind.
- Test `action_sections` (save/reload, unknown section, press path, toggle and
  press choice asked in the step's section). `press_mode` lambdas take the section.
- Manual: Section menu, the four sections, the jog limitation.

## Research behind the scope
- REAPER's own OSC surface (vendored `csurf_osc.cpp:2766-2772`) fires MIDIACTION /
  MIDILISTACTION through `MIDIEditor_LastFocused_OnCommand`, without a value.
- ReaLearn offers Main, active MIDI editor, active event list, Media Explorer.
- SDK has no call for the inline MIDI editor or the Media Explorer (forum uses
  WM_COMMAND to the explorer window, reported flaky). Left out.
- No SDK call hands a value to a MIDI Editor action; hence Main only for the
  relative jog kind.

## Open
- Not run in REAPER yet. Frank tests: picker on MIDI Editor returns the action,
  a MIDI Editor ReaScript resolves through NamedCommandLookup (Cockos' OSC takes
  only numbers for MIDIACTION, `if (cmd)`), toggle LEDs of MIDI Editor actions.
- Goes out with the next tag; changelog entry then.
