# Web TOML decoding boundary

## Four-reference investigation before implementation

Fresh native IDA MCP ASCII and UTF-16LE searches for `toml.dll`, `tomlDecode`,
and `tomlDecodeFromStorage` found none in the four authoritative mobile
references. UTF-16LE `xp3filter.dll` resolved in every database as a positive
control. Source searches, the actual plugin source list, and the existing
project's failed capability lookup independently confirm that the decoder is
not statically registered. This is a Web platform extension; it is not a
reconstruction of a missing function inferred from one search.

| Reference binary | Module wrapper | Internal loader | Internal registration | Plugins.link |
| --- | --- | --- | --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0x548e24 | 0x701de8 | 0x548d04 | 0x9086a0 |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0x4a9648 | 0x5ba8e8 | 0x4a9598 | 0x6c7e58 |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x100287b38 | 0x10029fde4 | 0x100287acc | 0x1003f2210 |
| Kirikiroid2_1.3.9_iOS_armv7 | 0x28a8a4 | 0x2a48fc | 0x28a7dc | 0x3d9884 |

All sixteen complete functions were freshly decompiled with the corresponding
database explicitly selected before C++ changes. Shared pseudocode:

```text
internalRegistration(): AllRegist(Pre); AllRegist(Class); AllRegist(Post)
                        LoadModule("xp3filter.dll")
LoadModule(input):
    key = lowercaseAscii(copy(input))
    if registered.contains(key): return false
    if internalMap.find(key) missing: return false
    run each module registrar in Pre/Class/Post order
    registered.insert(key); return true
Plugins.link(args): require first arg; convert to string; load; return OK
```

Android64 unrolls the three registration walks; the other builds loop three
times. Android uses libstdc++ linkage/helpers, iOS libc++; 32/64-bit pointer
widths, atomic guards, stack canaries and iOS armv7 SjLj cleanup differ. The
module key, duplicate/missing branches and post-success commit point agree.
No original loader, registration container or missing-module behavior changes.

## Supplemental supplied Windows protocol

The actual TOML DLL is PE32, image base 0x10000000, SHA-256
`107858e3a80c9f150814b340d0751291bfb71056a16dd005a76e7daa49ae8d76`.
Native IDA decompiled registration 0x10019030, storage decoder 0x1003e4c0,
string decoder 0x1003e9c0, parse helper 0x1001fca0, recursive conversion
0x100231e0 and table conversion 0x1001f300. This additional protocol evidence
does not replace the four mobile investigations.

The public methods are attached to `Scripts`. The storage decoder accepts a
required storage name and an optional non-void text-stream mode. It checks
existence, reads the complete existing TVP text stream, destructs it, parses
using the storage name for diagnostics, then publishes the converted result.
The string decoder accepts text and an optional string diagnostic name,
defaulting to `{imm-string}`. Success returns TJS_S_OK. Ordinary parse/open
failure clears the result, logs the diagnostic and returns TJS_E_FAIL. The
first required argument is handled by the supplied binder outside the bodies.

The converter maps booleans to TJS integer 0/1, integers to signed 64-bit,
floating values to real, strings through UTF-8/UTF-16 conversion, arrays to
native TJS Array (count first, indexed writes), and tables to native Dictionary
(recursive member writes). The original date/time branch reports unsupported
once and subsequently returns a null object. Its one-empty-key table envelope
can represent void/null or octets; string octets ignore non-hex characters and
pad an odd final nibble on the right, and array octets truncate integer bytes.
Those branches were checked in helpers 0x1001f190, 0x1001f040 and 0x10006410.

The DLL additionally exposes `tomlEncode` and has a `value:` comment extension
for serialized floating-point metadata. The Web decode scope does not expose
the encoder or claim its private comment serialization format; comments retain
ordinary TOML semantics. Standard numeric values remain numeric values. The
existing supplied UI table has no such floating-point comments.

## Local implementation correspondence and platform boundary

The browser cannot execute the supplied Windows DLL. A real Emscripten-only
`toml.dll` NCB registrar attaches the two decode callbacks to `Scripts` through
the unchanged loader above. The vcpkg baseline's toml11 4.4.0 provides the
standard TOML parser; native TVP text streams retain archive lookup, cipher/BOM
decoding and optional mode. No filesystem path is handed to the parser as a
replacement for TVP storage. The whole document is parsed even without a
requested result, and result publication follows conversion success.

The new converter uses native TJS Array/Dictionary objects and their normal
variant/reference ownership, not C++ containers as script objects. Root input,
recursive fields and optional diagnostic names come from the caller. There are
no game names, hashes, resource-name aliases, script patches or save rewrites.
Only the Web dependency/source list changes; non-Web builds and the existing
loader remain unchanged. The actual complete private package and its original
ciphered UI table provide validation material; no fixture is manufactured.

Parser API references: [toml11 parsing](https://toruniina.github.io/toml11/docs/features/parsing_files/)
and [value types](https://toruniina.github.io/toml11/docs/reference/value/).

## Existing-package validation

The pre-change complete-package run had no decoder and no `texts` object.
After the change its original encrypted storage was read by the game itself;
all 392 UI text entries were available, the selected language's save UI labels
resolved, initialization callbacks completed, and `logMode` stayed zero through
native save confirmation. A native manual slot with its chapter/comment was
created. These observations use the existing package, not a new fixture.
Encoder round trips, date/time inputs and private numeric comments are outside
this observed coverage. The distributed aggregate LICENSE includes toml11's
MIT notice.
