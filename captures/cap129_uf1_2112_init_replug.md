# cap129 — UF1 init sequence from SSL 360 2.1.12 (firmware 33109), StoerPC, 2026-09-11

**What:** 50 s USBPcap3 window during a physical unplug/replug of the UF1 while
SSL 360 2.1.12.72214 (Windows 11, Meter Pro 1.3.7 installed, no DAW running)
reconnected to it. Device address 18, OUT endpoint 0x02 / IN 0x81, 2.7 MB,
6642 FF frames: the complete 2.1.12 cold-connect init (`ff01 ff02 ff05 ff4b ff4e
ff38/39/3b` LED sweep, `ff1b` ×278, `ff1d/1e` fader dance, then the FF67 screen
paint) followed by the idle stream (0x0009/0x000a/0x0015/0x0016/0x011c/0x011d
at ~20 Hz).

**Why it exists:** the first SSL→UF1 capture after the 2.1.12 update (firmware
flashed 2026-09-10). Baseline for every "what is new on the UF1 display"
question, and the reference init to diff against `extension/src/uf1_init_sequence.inc`
(generated from cap101, SSL 360 2.0.6).

**How it was won (the part that cost an hour, see docs/capture-uf1-stoerpc.md):**
USBPcap attaches itself only as a class upper filter of class USB. SSL's drivers
put the UF1 control node into class `SSLUSBDriver_sc`, and after any driver
rebind USBPcap was simply not in that node's stack (`SSLBUS > USBHUB3`), while
the UF1's HID function and every plain USB device were captured. Fix: a
device-level `UpperFilters = USBPcap` on
`HKLM\SYSTEM\CurrentControlSet\Enum\USB\VID_31E9&PID_0025\UF1-009184`, then a
PHYSICAL replug (pnputil /restart-device does not rebuild the stack). Driver on
the node during this capture: `oem16.inf` (sslbus 2.12.36.4); 360 2.1.12 talks to
it fine. The 2.1.12 driver package (`ftdibus.inf`, oem13) was deleted from the
store to force that binding and is backed up at `C:\Users\claude\drv_backup_ftdibus`
— deleting it also removed `C:\Windows\System32\ftd2xx.dll`, which 360 needs;
restored by hand from the backup.

**Decode:** `analysis/uf1_screen_dump.py captures/cap129_uf1_2112_init_replug.pcap 18`,
`analysis/uf1_meter_capture_analyze.py … --dev 18 --timeline`.
