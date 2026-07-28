# real-core-handshake-2026-07-27

Real SSL 360° Core ↔ SSL Channel Strip plug-ins, captured on the **Mac loopback**
(`tshark -i lo0`) with real SSL 360° running (our impersonator off). Trimmed to the
SSL Core TCP streams only (`tcp.port==60250 || tcp.port==60249`). 12 plug-ins
connected to real Core on `127.0.0.1:60250`; the GUI is `60249↔60251`.

Purpose: settle the **load-countdown** root cause without guessing (Frank insisted on
a capture after a day of circular theories). See [[regression-load-countdown-2026-07-26]].

## Frame format (magic `ef bc 51 00`, all little-endian)
```
off  size  field
0    4     magic  ef bc 51 00
4    4     body length (bytes that follow)
8    4     const 0x00000010 (16)
12   4     const 0x00000001 (1)
16   4     seq        (per-connection id after handshake; 0 during pre-handshake)
20   4     payload length (= 12 + pb length)
24   4     TYPE       ← message type
28   4     scopeId    0x5f7a0579
32   4     msgId      0x0000011c
36   ..    protobuf-ish payload (pb)
```

## Connect handshake (one plug-in), in order
```
P→C  type-4   08 05 10 <plugin uniqueId varint> …   plug-in announces identity
P→C  type-5
C→P  type-19  serverConfig
C→P  type-6   (empty payload; IDENTICAL for every plug-in)
C→P  type-7   seq=<connId>  pb: 08 <connId varint>   ← UNIQUE object id per plug-in
C→P  type-19  serverConfig(dataPort)
… then type-2 (object register ×~47), type-18 (subscribe ×5), type-10 (heartbeat) …
```

## The finding
- Core sends **both** type-6 and type-7 to **every** plug-in (all 12) — not "only the
  focused one".
- **type-7 pb + seq are UNIQUE per connection** — 12 plug-ins ⇒ 12 distinct type-7
  payloads. It is an assigned object id, NOT an "activate this channel" command.
- Our impersonator hardcoded `ctrlFrame(7, 0x9abc)` for all → every plug-in believed it
  was the same (focused) object → each selected its own track = the countdown.
- Fix: unique `connId` per connection (`openingSequence(dataPort, connId)`).

## Other message types seen Core→plug-in
`type 2` object register, `type 10` heartbeat (our `heartbeat()`), `type 18` meter
subscribe, `type 19` serverConfig. Plug-in→Core floods `type 16/17/18` (param register +
values + state). Useful reference for any future impersonator faithfulness work.
