# foot_02_foot1_tap

Frank pressed Foot 1 twice during the 8 s capture (momentary footswitch).
Decoded IN-endpoint frames (after stripping the 2-byte USBPcap wrapper):

```
t=1.37  FF 22 03 00 00 01 26   press
t=1.68  FF 22 03 00 00 00 25   release
t=2.78  FF 22 03 00 00 01 26   press
t=3.35  FF 22 03 00 00 00 25   release
```

Same `FF 22 03 <id> 00 <state> <ck>` frame as the other UF8 buttons:
**Foot 1 = id 0x00**, state=0x01 pressed, state=0x00 released.
Checksum = byte-sum of the 4 payload bytes (`22 03 id state`) mod 256.
