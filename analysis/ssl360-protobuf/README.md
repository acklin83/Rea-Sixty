# SSL 360° plugin protobuf protocol (reverse-engineered)

Reconstructed 2026-07-10 from `SSL Meter Pro.vst3` (Meter_Pro_Mac_v1_2). The
plugin embeds a Google-Protobuf `FileDescriptorProto` (`AssignerArgsTypes.proto`);
we pulled it straight out of the Mach-O and decoded it with a minimal wire
parser. Full machine reconstruction: `AssignerArgsTypes.reconstructed.proto.txt`.

This is the protocol SSL plugins use to talk to **SSL 360°Core** — the piece
that, until now, forced 360° to stay running for the UF1 Meter view. It covers
far more than metering (EQ curve, channel-strip/bus-comp meters, LEDs, gestures,
enums for every 360° param), but the immediate target is the **UF1 Meter/Analyzer
display** — the main UF1 selling point.

## Transport (from the schema + binary strings)

- Plugin = `TCPClientConnection`; finds Core via **UDP broadcast** discovery.
- `LgxPropertyConnectionAnnouncementData { AppVersionMajor, AppVersionMinor,
  string IpAddress, uint32 Port, string MachineName }` — the announcement that
  tells a peer where to connect.
- `ServerConfigMessage { int32 UDPDataPortToCore, string LgxVmRootScopeName,
  int32 Regular }` — Core→plugin config.
- `PluginConfigMessage { PluginType, InstantiationTime, PluginHost Daw,
  PluginCodeVersion, PluginWrapperType HostType, string PluginVersionNumber,
  uint32 TargetViewModelVersion }` — plugin→Core handshake.
- **iLok/PACE Eden Thrift + encryption in the binary is the LICENSING path only**
  — a separate channel. The plugin↔Core data path is plain protobuf (to confirm
  on the wire with a loopback capture).

## Wire framing

Every message on the wire is a **`PluginProtoBufExporter`** with exactly one
field set (a hand-rolled oneof — 63 optional fields). The ones we care about:

| field | message | meaning |
|------:|---------|---------|
| 17 | `PluginMeterDataMessage` | **live meter values** (the payload we want) |
| 19 | `PluginMeterDataReference` | which meter/plugin a data stream refers to |
| 59 | `PluginMeterPrepareEventArgs` | per-meter legends/units/range/format |
| 60 | `PluginTextMeterPrepareEventArgs` | numeric-readout formatting (decimals, peak) |
| 34/35 | `PluginEQCurveDataValueEventArgs` / Prepare | **real EQ curve** `repeated float m_dBValues` (could replace our parametric Channel-view render) |
| 13/18 | `PluginConfigMessage` / `ServerConfigMessage` | handshake/config |
| 3 | `LgxPropertyConnectionAnnouncementData` | discovery |

## The meter payload — this is the deliverable

```proto
message PluginMeterDataMessage {
  optional PluginType PluginType   = 1;   // EPT_MeterPro = 8, EPT_Meter = 4
  optional int32  DataType         = 2;   // MeterPluginDataType (below)
  repeated float  CurrentMeterValues = 3; // <-- live values, already computed
  repeated float  PeakValues       = 4;   // peak-hold companion
  repeated bool   OverloadValues   = 5;   // clip flags
  repeated bool   OverloadInfHoldValues = 6;
  optional int32  MaxValueCount    = 7;
  optional int32  ChunkSize        = 8;   // large arrays (history) are chunked
  optional int32  ChunkOffset      = 9;
  optional int64  time             = 10;
}
```

`DataType` (`MeterPluginDataType` enum) → maps 1:1 onto the UF1 meter modes.
`CurrentMeterValues` length depends on type:

| DataType (#) | values | UF1 mode / element |
|---|---|---|
| `VuPpm` (0) / `TextVuPpm` (1) | L,R needle / text | **Analogue** (0x0125/0x0127 needles + 0x011c) |
| `BarPeak` (2) / `BarRms` (3) | L,R bar heights | **Overview** bargraphs |
| `TextPeak` (4) / `TextRms` (5) | value/max/current text | **Overview/all** numeric row 0x011c |
| `Correlation` (6) | 1 float | Overview phase-correlation (0x0127) |
| `StereoBalance` (7) | 1 float | Overview L-R balance (0x0126) |
| `Rta31Band` (8) / `TextRta` (9) | 31 floats + sel-freq | **RTA** spectrum (0x0122) |
| `Lissajous` (10) | scatter points | **Overview** scope (0x0122) |
| `Loud_Momentary` (11) / `ShortTerm` (12) / `RangeLow` (13) / `RangeHigh` (14) | LKFS floats | **Loudness** (0x000c / 0x011c) |
| `Loud_Readout1..10` (15..24) | configurable readouts | Loudness text panel |
| `Loud_CompleteHistory` (25) / `ScrollableHistory` (26) / `Histogram` (27) | chunked arrays | Loudness history graph (0x0122) |

`PluginMeterPrepareEventArgs` carries the per-type **Legends[]**, **Units**,
`MaxRangeStart/End`, `ClearValue`, `NumEnabledValues` — everything needed to
scale a value into the UF1 codec without guessing.

Also present: `ChannelStripMeterType` (Input/InputRms/Output/OutputRms/CompGain/
GateGain/MicPreSaturation) and `BusCompMeterType_GainReduction` — the same
protobuf path yields **channel-strip and bus-comp GR metering**, not just the
Meter plugin.

## WIRE-CONFIRMED (cap87, loopback 2026-07-10)

Meter data = **plain protobuf over UDP**, unencrypted. One Core data port per
meter instance (seen: 16010, 50881; 16008/16009 = announcement). Framing:
`[ef bc 51 00][u32 frame_len][28-byte SSL header][PluginMeterDataMessage]`.
Header = `u32{16,1,seq,innerLen,3,streamId,269}`; protobuf at offset 28. Decode
with `decode_capture.py`. Full DataType→length→value table in
`captures/cap87_ssl360_meter_protobuf.md`. Silence sentinel float = `ff c0 00 00`.

Confirmed live values: BarPeak −10.44..−4.32 dBFS, Correlation 0..0.95,
StereoBalance −0.07..0.09, Lissajous 0..1, Loudness LKFS floats, RTA 31-band.
Two instances streamed on two ports simultaneously (the multi-channel case).

## Build path

Two options for wiring this into our extension:

- **(A) Impersonate 360°Core** — open the TCP server + UDP announcement the
  plugin expects; the plugin connects to us and streams meter protobufs. Full
  360° replacement for metering. The real product.
- **(B) Passive loopback sniff** (360° running) — decode-only, for format
  validation. Not shippable (360° claims the USB exclusively).

Immediate next step: a **loopback capture** of the plugin↔360° traffic to confirm
plain-protobuf-on-the-wire + capture sample `PluginProtoBufExporter` frames, then
implement (A). We do NOT self-DSP meters (Frank, hard rule 2026-07-10) — every
value comes from the plugin's own `PluginMeterDataMessage`.

## PluginType enum

`EPT_Unknown=0, NativeChannelStrip=1, NativeBusComp=2, 4k_B=3, Meter=4, 4k_E=5,
GenericChannelStrip=6, GenericBusComp=7, MeterPro=8, 4k_G=9`.
