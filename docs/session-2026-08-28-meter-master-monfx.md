# 2026-08-28 — V-Pot1 could not switch between the master and the monitoring chain

Frank: *„Wenn ich Instanz auf Master und auf Monitoring FX hab, dann kann ich am
Gerät nicht mehr umschalten ausser ich wechsle den Kanal mit dem Encoder."*
Later, from the Windows box: *„auf windows funktioniert Meter Loudness page
nicht, die VU anzeigen gehen nicht etc."*

Confirmed fixed on hardware at the end of the day: *„ja, jetzt heissen sie auch
master und mon fx."*

## What was actually wrong

Three separate defects stacked on the same symptom. Only the third one was the
reason it still failed after the first two were fixed.

### 1. `-1` is a real track index, `0` is "not stated" (`a1facc5`)

The plug-ins announce their track in REAPER's `IP_TRACKNUMBER` convention, where
the master reports **-1**. Two guards treated everything `<= 0` as unknown:

- `meterPortForFx()` returned 0 for `trackIndex <= 0`, which switched the whole
  stream-to-FX resolver off on the master. The caller kept its first-match
  fallback, so V-Pot1 moved the *data* while the label, V-Pot2/3/4, their
  readouts and the preset browser stayed on the first Meter.
- `steerAutoPort_()` bailed on `want <= 0`, and `GetSelectedTrack` **ignores the
  master** (SDK: "see GetSelectedTrack2"), so auto-mode could never follow a
  Meter on the master at all. `uf1PaintMeter_` now asks the master's
  `I_SELECTED` when no ordinary track is selected.

New **rung 0 CHAIN** in `meterPortForFx`, ahead of settings and pro-ness: the FX
index's `0x1000000` flag is the monitoring chain, and that is the chain the host
names `HARDWARE OUTPUT`. Nothing is inferred from connect order. The ordinal rung
is skipped once rung 0 narrowed the list, because the ordinal counts across both
chains and would otherwise point at the neighbour.

The instance NUMBER now counts only meters that would label identically (same
track index **and** same announced name), so the master's two chains get no
digit. Label reads `MASTER` and `MON FX`.

### 2. Windows could bind next to a live SSL Core, silently (`02262fb`)

`SO_REUSEADDR` means the opposite on Windows. On macOS/Linux a second bind next
to a live listener fails with `EADDRINUSE`, which is the signal this file already
reports as "in use? 360 running?". On Windows it "allows a socket to forcibly
bind to a port in use by another socket", after which the delivery is
"indeterminate" (learn.microsoft.com, *Using SO_REUSEADDR and
SO_EXCLUSIVEADDRUSE*). Windows now sets no option at all: the default bind fails
on a live conflict and ignores our own TIME_WAIT leftovers. Deliberately **not**
`SO_EXCLUSIVEADDRUSE`, which by Microsoft's own description can lock a listener
out of its port after it accepted and closed a connection.

Also: every line that explains a dead meter went through `slog()`, which writes
nothing unless the trace env var is on. Worker up (with how many of the six UDP
ports it got), each bind failure with its error number, and the per-connect
instance announcement now go to the ordinary `rea_sixty.log` via `slogAlways()`.

**This was not the cause of Frank's bug** — see the measurements below — but it
is a real hazard and the hardening stands.

### 3. The actual cause: a half-read varint (`8995d87`)

Protobuf sign-extends a negative int32 to 64 bits, so `-1` arrives as **ten**
bytes: `08 ff ff ff ff ff ff ff ff ff 01`. The decoder stopped at `shift <= 28`,
read five of them, and its last accumulate was `int(0x7f) << 28` — signed
overflow, undefined behaviour. The two compilers disagreed:

| | instance A | instance B |
|---|---|---|
| macOS (clang) | -1 | -1 |
| Windows (MSVC) | -1 | **127** |

`127` is `0x7f`, exactly one byte of that varint. Two identical instances landed
in two different track spaces, `liveMeterPorts_(-1)` found one of each, the
resolver answered `by sole instance`, and V-Pot1 had nothing to switch between.

Now accumulated in `uint64_t` across all ten bytes and narrowed once at the end.
The bytes are logged next to the result.

## Two hypotheses killed by measurement, not by argument

Both of my Windows theories were wrong, and the same single run disproved them:

```
[sslcore] worker up: TCP :52716  UDP data: 16010 … 50882 (6 of 6)
[uf1] cycle pacer: tail slot overshoot avg 1.00 ms, max 2.20 ms over 100 cycles
```

All six ports free, so no port conflict on that box. And the OS timer resolution
is fine — 1 ms, not the 15.6 ms a Windows scheduler tick would have imposed on
the Overview pacer's 18.0/40.3 ms offsets. The pacer measures and reports this
itself rather than assuming; that instrumentation was added instead of the
timer rework I was about to build.

**A symptom that appears on only one platform does not prove a
platform-dependent cause.** Undefined behaviour looks exactly the same from
outside.

## The Windows box was also 15 days stale

Before any of this could be tested, StoerPC turned out to be running a DLL from
13.08., config version 24 against main's 32, on branch `uf1-native-build`. It
predated the monitoring-chain support entirely. Rebuilt from `main` and
deployed to `C:\Users\sunny\...\UserPlugins` (the account Frank runs REAPER
under, *not* the `claude` account SSH uses). Old DLL kept as
`.bak_2026-08-13`, `bindings.json` backed up before the v24 → v32 migration.

## Doc fix done on the way (`a5fe9db`)

`.local-docs/release-process.md` hardcoded `192.168.177.197` in 29 places. The
box had been `.198` since 2026-07-24. Chasing the stale address cost a round of
debugging against a machine that was not on the network. The runbook now
resolves the address instead of remembering it, and every command takes
`$STOERPC`. `tailscale status | grep stoerpc` answers "is it up" and "what is
its LAN IP" in one line; ping proves nothing, because Windows drops ICMP by
default — scan 445 instead.

## Open

- Loudness page on Windows: reported alongside this, never separately
  reproduced. The box was two weeks stale at the time, so it needs a fresh look
  on the current build before it counts as a bug.
- `resolveStripPort_` carries the same `trackIndex <= 0` guard as the meter had,
  so an SSL channel strip on the master cannot resolve its stream either. Left
  alone deliberately: all three callers gate on `trackIdx > 0` themselves, so
  fixing the guard alone changes nothing, and the caller-side change is out of
  scope for this report.
