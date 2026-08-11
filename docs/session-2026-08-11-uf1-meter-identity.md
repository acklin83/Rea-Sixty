# 2026-08-11 — UF1: which SSL Meter instance the surface is talking to

Branch `uf1-native-build`. The morning's five fixes (`fb6ea66` … `2e8de70`) came
out of Frank's own hardware finds and carry their reasoning in their commit
messages; this note covers the afternoon's one item, which was the last thing on
the handoff list that had never been started.

## The bug

`uf1PinnedMeterTrackFx_` resolved the TRACK from the stream's `HostTrackIndex`
and then took `uf1FindMeterFx_(tr, 0, …)` — the first SSL Meter in the FX chain.
`steerAutoPort_` picked the stream by walking `g_inst`, a map keyed by UDP source
**port number**. On a track with two Meter plug-ins those are two independent
answers to the same question, so the V-Pots edited one instance while the display
drew the other. Nothing looked broken: both showed plausible numbers.

This is the identity problem the channel strips had until 2026-08-10, one plug-in
over, and it survived next to that fix precisely because the symptom is invisible.

## The fix

`sslcore::meterPortForFx()` — the strips' resolver, same three rungs, same rule
that an ambiguous rung **defers instead of picking**:

1. **Settings.** The Meter announces its parameters exactly like the strip does:
   a `type=16` frame declares the wire short-id, a `type=18` frame carries the
   value.
2. **Pro-ness.** A Meter Pro streams Loudness data types; a plain Meter never
   does. Positive evidence only — `isPro == false` means "has not shown Loudness
   yet", so both directions require that some instance on the track HAS shown it.
3. **Ordinal** among the track's Meters in connect order — which is FX-chain
   order, and now also the order `steerAutoPort_` picks in.

`steerAutoPort_` takes the first of `liveMeterPorts_()` instead of the first
entry of a port-keyed map, so the display's pick and the V-Pots' pick are derived
from one ordering.

## The wire ↔ REAPER table (`meterFingerprintIds`, SslCoreImpersonator.h)

Wire ids read off the plug-in's own `type=16` declarations in the sslcore trace;
VST3 indices from `docs/ssl-native-params/VST3__SSL_Meter_Pro_(SSL).md`; the two
tied together by comparing each streamed value against that dump's current value.
Only pairs that agree are in the table.

**The wire sends the ENUM INDEX for stepped parameters and real units only for
continuous ones.** Confirmed on five independent params:

| param | wire | REAPER | |
|---|---|---|---|
| Digital Peak Hold | 2 | 0.5 × (5-1) | enum |
| 0 VU Line-Up | 1 | 0.5 × (3-1) | enum |
| Analogue Max Needle | 1 | 0.5 × (3-1) | enum |
| Lissajous Fade Time | 4 | 0.6667 × (7-1) | enum |
| RTA Peak Hold | 4 | 0.5714 × (8-1) | enum |
| RMS Integration | 500.0 | "500.0 ms" | real |
| Analogue Reference Level | -18.0 | "-18.0 dBFS" | real |

RTA Scale Top/Bottom are enums **on the wire** even though the V-Pot table drives
them continuously — Bottom streams `11`, not `-120 dB`. That is a V-Pot feel
decision and says nothing about the protocol; read as a real unit it would never
match and the rung would quietly do nothing.

The fingerprint is offered for a Meter **Pro** only. The plain Meter's parameter
indices have never been dumped, and reading the Pro's indices off it would be
numbers from the wrong parameters. Pro-ness already separates those two.

## Also

- V-Pot1's label numbers the instances when a track carries more than one
  ("Track 4 2"). Both announce the same `HostTrackName`, so the label read
  identically for both and cycling the pin looked like it had stuck.
- The resolved FX is cached for 500 ms: `uf1PinnedMeterTrackFx_` is called from
  every meter paint block, several times a tick, and each candidate costs a dozen
  `TrackFX` reads. Single-Meter tracks skip the whole walk (`cnt > 1` gates it).
- With `ssl_core_trace=1` the resolver prints
  `[meter] track N fx#M -> port P by settings|pro-ness|ordinal` on every change
  of answer.

## Open

Whether the Meter **re-sends** a parameter after it changes is unproven. The
strip does. The trace only ever caught the Meter's connect-time dump, because
nobody moved a Meter parameter that session. If it does not re-send, rung 1 goes
stale after an edit and the ordinal carries it — stable, not wrong, but the
settings rung would be decoration. The trace line above answers it in one pass on
the hardware.
