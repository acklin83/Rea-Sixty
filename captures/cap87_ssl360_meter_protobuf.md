# cap87 — SSL 360° plugin→Core meter protobuf (loopback)

**Date:** 2026-07-10
**Host:** Frank's Mac Studio. **Capture:** `sudo tcpdump -i lo0` while two SSL
Meter Pro instances metered live audio in REAPER with SSL 360° running.
**File:** `cap87_ssl360_meter_protobuf.pcap` — carved from a 372 MB full-loopback
capture down to the SSL Core UDP flows only (~4000 pkts, 14.5 MB). Decoder +
schema: `analysis/ssl360-protobuf/`.

## Headline: SSL meter data is plain, UNENCRYPTED protobuf over UDP

This is the data source for the UF1 Meter/Analyzer view (and for gate-GR,
bus-comp GR, EQ curve — same protocol). No DSP, no pixel-reverse, no 360°
needed once we impersonate Core. See memory `ssl360-plugin-protobuf-protocol`.

## Wire format (confirmed)

Meter data flows **plugin → 360°Core over UDP**, one Core data port per meter
instance (this capture: **16010** = a master/surround instance, **50881** = a
stereo instance; **16008/16009** = `LgxPropertyConnectionAnnouncementData`
handshake, `IpAddress`/`Port`/`MachineName` byte-exact to the reconstructed
schema).

Each UDP datagram = one or more frames:
```
[ef bc 51 00][uint32 LE frame_len][28-byte SSL/Lgx header][protobuf]
```
28-byte header: `u32=16, u32=1, u32 seq, u32 innerLen, u32=3, u32 streamId,
u32=269(meter-msg marker)`. Then a `PluginMeterDataMessage`:
`f1 PluginType, f2 DataType, f3 repeated fixed32 CurrentMeterValues (tag 0x1d),
f4 repeated fixed32 PeakValues, f5/6 Overload bools, f7 MaxValueCount…`.
Silence sentinel float = `ff c0 00 00` (−inf).

## Decoded DataType map (10,998-packet run + carved-pcap replay)

| DataType (#) | curN | value semantics (observed) |
|---|---|---|
| VuPpm(0)/TextVuPpm(1) | 2 (stereo) / 12 (multi) | needle/text dBFS |
| BarPeak(2) | 2 / 12 | dBFS, e.g. −10.44..−4.32 |
| BarRms(3) | 2 / 12 | dBFS, −129.6..−20.5 |
| TextPeak(4)/TextRms(5) | 2 / 12 | dBFS readouts |
| Correlation(6) | 1 | −1..1 (seen 0.0..0.95) |
| StereoBalance(7) | 1 | −1..1 (seen −0.07..0.09) |
| Rta31Band(8)/TextRta(9) | 31 | per-band dB (−inf when silent) |
| Lissajous(10) | 613–5000 | scope points, 0..1 (fade-dependent count) |
| Loud_Momentary/ShortTerm/Range(11–14) | 1 | LKFS (−70 floor) |
| Loud_Readout1..10(15–24) | 1 | configurable LKFS/dBTP readouts |
| Loud_CompleteHistory(25) | 771 | history plot, 0..1 |
| Loud_ScrollableHistory(26) | 613 | scrollable history |
| Loud_Histogram(27) | 120 | loudness histogram |

`CurrentMeterValues` array length = **stereo(2)/mono(1) vs channel-format(12)**;
`PeakValues` mirrors it (empty for scope/history types). `f1 PluginType` seen
{3,9} on 16010, absent on 50881 — instance-dependent, not blocking.

**Multi-instance answer:** each SSL Meter instance streams to its own Core UDP
port → our Core-impersonator enumerates instances by port/announcement; V-Pot-1
selector picks which one drives the UF1 (matches manual S.180/189).

## Next
Build the Core-impersonator: reply to the announcement, open the Core UDP data
port(s), receive `PluginMeterDataMessage`, feed the UF1 per-mode codec. NO
self-DSP — every value is the plugin's own float.
