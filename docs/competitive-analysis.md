# Competitive analysis & battle plan

*What rival control-surface solutions do better than Rea-Sixty, and the prioritised plan to close (or deliberately ignore) each gap.*

Scope: measured against Rea-Sixty's actual mission — an **open-source SSL 360° replacement for the UF8 / UC1 on REAPER**. Gaps are only "real" if they matter to that mission. Chasing parity with tools built for a different mission (multi-DAW hosts, universal MIDI mappers) is explicitly out of scope and called out as such below.

Date of survey: 2026-06-24. Re-check when SSL ship a new 360° major version or when ReaLearn/Helgobox ship a release that changes the surface story.

---

## The field

Four solutions are relevant; each beats us at something different.

### 1. SSL 360° V2.0 — the direct competitor

The thing we replace. SSL caught up in 2025. Where it still wins:

| Their feature | Our status |
|---|---|
| **On-screen Plug-in Mixer** — every CS/BC instance in one window, fully interactive | **Phase 2.6 PENDING.** Our docked window hosts Settings tabs only. This is *the* gap that still makes a user glance at SSL 360°. |
| **Collapse & Pin** — tidy multiple 360°-enabled strips on a single DAW track | Not implemented. Instance-Cycle walks instances; there is no pin / collapse. |
| **Hybrid Mode** — DAW control layer *and* Plugin-Mixer layer at once: automation soft-keys + favourite DAW shortcuts straight from the Plugin-Mixer | We have separate modes, no hybrid overlay. |
| **UF1 support**, 13 DAW workflow templates, **Multi-DAW (3 at once)** | UF1 unsupported; Multi-DAW is a deliberate non-goal (architecture-decision.md). |
| **In-app firmware update** | Deliberately left to SSL 360° (Phase 4). |

### 2. ReaLearn / Helgobox — the most powerful mapping engine on REAPER

Open source, mature, years of polish. Beats our bindings clearly at:

- **Conditional Activation** — mappings / groups / whole instance active by context (shift, mode, active FX). Our bindings are flat by comparison: name-substring FX-Learn + modifier combos + soft-key banks.
- **Projection / Companion app** — projects a **live schematic of the controller to a phone/tablet**, including current parameter values. We have nothing like it — and crucially, *no SSL-world competitor does either*.
- **Lua MIDI scripting, OSC, StreamDeck, keyboard** as sources + feedback.
- **Auto-load preset depending on active FX.**

### 3. DrivenByMoss & 4. CSI — the generic REAPER surfaces

- **DrivenByMoss**: many controllers + multi-DAW, adjustable sensitivity/layouts, project actions from hardware.
- **CSI**: extremely deep, fully text-based configuration; **huge community template library**; strong accessibility community.
- Shared lead over us: **ecosystem & config sharing.** Hundreds of ready-made configs to trade. Our config Import/Export is still **Phase 2.7e PENDING**.

---

## Battle plan

Ordered by leverage (impact ÷ effort), not by roadmap number.

Guiding principle: do **not** try to out-map ReaLearn or build multi-DAW — those are losing fights against deliberate architecture decisions. Instead, close the SSL 360° gap, then leapfrog with one feature nobody in the SSL camp has.

### Wave 1 — Reach parity (mandatory; closes SSL 360° gaps)

1. **Finish the Plug-in Mixer window (Phase 2.6b–d).** Top priority. Already specced; the foundation (docked window + ThemeBridge, Phase 2.6a) is done. While this is missing, SSL 360° stays open — which undercuts the whole project promise.
2. **Config Import/Export + Reset-to-Defaults (finish Phase 2.7e).** Cheap, but strategic: it is the entry ticket to the community ecosystem that drives CSI/ReaLearn adoption. No sharing → no network effect.

### Wave 2 — Ergonomic parity with V2.0

3. **Hybrid layer overlay** — DAW functions (automation soft-keys, favourite shortcuts) reachable without leaving the plugin context. Counterpart to SSL Hybrid Mode; fits the existing EncoderMode/overlay pattern (same family as Nav Mode).
4. **Collapse & Pin** for multi-CS/BC tracks — likely a small extension of Instance-Cycle (a pin flag per instance).

### Wave 3 — Leapfrog (something nobody in the SSL camp has)

5. **Second-screen / Projection.** We already render ReaImGui in the docked window — a read-only surface-mirror view (strip colours, values, meters, active mode) on a tablet / second monitor is the natural next step and directly counters ReaLearn's strongest differentiator, inside the UF8/UC1 market. Differentiate rather than chase.

### Deliberately NOT pursued (reasoned rejections)

- **Multi-DAW** — architectural non-goal, stays out.
- **Full ReaLearn mapping depth (Lua scripting, etc.)** — anyone who needs that runs ReaLearn *alongside* us; not our fight. A small subset — *conditional bindings* (FX-/mode-dependent) — is a maybe, but low priority.
- **In-app firmware** — leave it to SSL 360° (Phase 4).
- **UF1 support** — assess separately: a genuine hardware gap vs SSL, but large decode effort. Phase 4 candidate, not now.

---

## Recommendation

Wave 1 immediately. Item 1 (Plugin Mixer) is the single remaining reason to open SSL 360° at all; item 2 is nearly free. Item 5 (Projection) is the mid-term differentiator that moves us from "as good as SSL 360°" to "better than anything in the UF8/UC1 market".

## Roadmap mapping

| Battle-plan item | Roadmap phase | Action |
|---|---|---|
| 1. Plugin Mixer window | 2.6b–d (PENDING) | Promote to next build. |
| 2. Config Import/Export + Reset | 2.7e (partially shipped) | Finish the two pending bullets. |
| 3. Hybrid layer overlay | *new* | Add as a surface-mode peer (Nav/Lanes/Group family). |
| 4. Collapse & Pin | *new* | Extend Instance-Cycle with a pin flag. |
| 5. Second-screen / Projection | *new* | New phase after 2.6; reuses the ReaImGui host. |
| UF1 support | Phase 4 candidate | Defer; large decode effort. |

## Sources

- SSL 360° V2.0 — [Production Expert](https://www.production-expert.com/production-expert-1/solid-state-logic-release-ssl-360-v20-software-for-u-series-controllers-and-interfaces), [Solid State Logic](https://solidstatelogic.com/products/ssl-360)
- ReaLearn — [Helgoboss](https://www.helgoboss.org/projects/realearn), [Helgobox](https://www.helgoboss.org/projects/helgobox), [Companion app](https://github.com/helgoboss/realearn-companion), [Conditional activation #231](https://github.com/helgoboss/helgobox/issues/231)
- DrivenByMoss + REAPER — [iCON Pro Audio](https://iconproaudio.com/2025/10/explore-the-full-power-of-reaper-with-drivenbymoss-and-icon-pro-audio-controllers/)
- CSI — [CSI v3 (ReaLinks)](https://www.realinks.net/links/csi-control-surface-integration-extension/), [CSI FAQ (The REAPER Blog)](https://reaper.blog/2021/04/csi-faq/)
