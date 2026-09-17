# 2026-09-17 — Issue #8: the SSL protocol, read properly for the first time

`sollapse` opened [issue #8](https://github.com/acklin83/Rea-Sixty/issues/8) with
twelve findings about our SSL 360 handling, each tagged measured or unverified,
each linked into our source at `666dbbf`. Every one of the twelve describes our
code correctly. Nine are now fixed or answered.

## The two rules that explain almost all of it

**1. Every object id is a hashed clear-text name.** `h = 101*h + c`, 64-bit,
little-endian on the wire. It reproduces `kScopeId`/`kMsgId` as `"system"`, the
README's `streamId` as `"meters"`, and all eleven named ids this repo already
carried as byte tables. It lives in `SslCoreImpersonator.cpp` as `sslScopeHash`
with fourteen `static_assert`s: a wrong name stops the build.

**2. A field sitting at its protobuf default is omitted.** Which is why
`DataType 0` — VuPpm, the analogue needle — arrives with no type field, and why
a TCP value of exactly 0 arrives as an empty body.

Everything below follows from one of those two.

## What landed

| # | Commit | What |
|---|---|---|
| 1 | `879ba9d` | An absent data type means VuPpm. The needle emulation was working around our own parser. |
| 2 | `60cac4b` | A fingerprint value of exactly 0 is stored instead of dropped. |
| 3 | `ea31292` | The goniometer is 17113 four-bit cells, not 8557 one-byte pixels. Gamma, resampler and brightest-cell rule deleted. |
| 4 | `712a302` | One Meter holds the selection LED, and it follows the surface. |
| 5 | `247254a` | The plug-in type comes from its hello, and an instance exists before it streams. |
| 6c | `577cc32` | Only message type 3 is parsed as a meter. |
| 7 | `853d37c` | Prepare messages are read: legend, unit, value count, overload mode. |
| 8 | `1d66052` | A mono stream gets the mono faceplate. |
| 9 | `853d37c` | Loudness captions come from the plug-in, not from our table. |
| 11 | `be321ff` | The EQ graph's 250th point is the curve, not a constant 0 dB. |
| 12 | `e6c2bff` | `HostTrackUuid` exists and REAPER leaves it empty. Answered, nothing to build. |
| — | `e6c2bff` | The hash itself, with the compiler checking every name. |

Open: **10** (multi-channel Overview) needs a surround Meter Pro. **6a/6b** (the
discovery ports) are left alone: no gain, real risk.

## The probe

`REASIXTY_SSL_PROBE=1`, bridged from ExtState `rea_sixty/ssl_probe`. Walks raw
datagrams and TCP bodies **beside** our parsers and writes
`<tmp>/reasixty_ssl_probe.log`. Three runs produced `captures/cap139`.

## Three times the instrument decided the answer

Worth writing down because it is the same mistake at three scales, all in one
day, and the first two had been shipping for weeks.

1. **`46ef899` counted data types in our own dump** and concluded SSL had dropped
   VuPpm. The dump comes from a parser that discards exactly those frames.
2. **The first probe logged only frame types 16, 17 and 18** — the three our code
   already reads — so it could only confirm what we knew, and it filtered out the
   plug-in hello, the one message that answered the open question.
3. **The second probe deduped by (type, scope, signature)**, which collapsed three
   plug-ins' hellos into one line that could not be attributed to any of them.

Rule, now in memory and in the code: a measurement that runs through our own
decoder is a statement about us. Measure beside it, not through it.

## One suggestion that did not survive contact

Finding 6 proposes filtering meter datagrams on `type == 3 && scope ==
hash("meters")`. Our frames carry `hash("system")` there, all five real vectors
from cap87. Adopting it verbatim would have rejected every meter we receive. We
filter on the type alone.

## The selection LED, twice

First attempt (`819af79`) was reverted the same afternoon. Instance existence
meant "streamed within three seconds", so selecting one Meter erased the others
from the list used to select between them. Frank saw the channel encoder select
a track in REAPER while the UF1 sat on Track 1, and Meter mode showing one Meter
Pro where five were loaded. The fix was not to work around it but to take
identity off the data path (`247254a`), after which the LED went back in.
