# Web function-key input boundary

The Web SDL-to-Cocos table drops F1–F12 before the existing engine key
conversion runs. Browser capture records both F2 events while the game's save
menu stays closed. This is an input-platform boundary: SDL key symbols must
be translated to Cocos key codes before the shared engine receives them.

## Reference evidence checked before implementation

All four binaries and their matching IDBs were checked for readability. The
following functions were decompiled through native IDA MCP in this session;
their lookup tables were read independently.

| Reference binary | Engine key converter | Lookup table |
| --- | --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `TVPConvertKeyCodeToVKCode`, `0xa97c60` | `0x1553c90` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `TVPConvertKeyCodeToVKCode`, `0x7b26f0` | `0xdf2bc0` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `sub_100E1876C`, `0x100e1876c` | `0x101813a20` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `sub_C24618`, `0xc24618` | `0x169e4b0` |

The common control flow is:

```text
index = unsigned(key - 1)
if index > 164: return 0
return keyTable[index]
```

Every table maps Cocos key codes 47–58 to engine virtual keys `0x70`–`0x7b`
(F1–F12). All four use the same bounds, default and function-key values.
ARM64 decompilation expresses the out-of-range branch first; ARMv7 expresses
the in-range branch first. Table addresses and load instruction forms differ
with architecture; no function-key behavior differs.

The existing local `CCKeyCodeConv.cpp` already routes the twelve Cocos symbols
to the corresponding virtual keys. It is unchanged. The Web adapter keeps its
existing table lookup and `KEY_NONE` default, adding only the twelve SDL-to-Cocos
entries. Both key down and key up already pass through that adapter. There is
no game, script, filename or package condition in the conversion.

Validation uses the existing real-game browser evidence: captured F2 down/up
without a save menu before the change, then native F2/F3 save/load after an
explicit candidate rebuild. Private game assets and screenshots stay outside
Git. This change does not add a synthetic Cocos/SDL enum fixture.
