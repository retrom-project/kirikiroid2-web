# Web PSB encrypted-header boundary

## Fresh four-reference mapping

The four binaries and matching readable IDBs were checked before native MCP
analysis on 2026-11-13. Each address belongs only to its named binary. All twelve
functions below were freshly decompiled in this conversation.

| Binary | LoadStorage | Adopt | Seed-filter invoker |
| --- | --- | --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0x598918 | 0x598AE8 | 0x6837AC |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0x4DD0A0 | 0x4DD200 | 0x56522E |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x1000ED468 | 0x1000ED654 | 0x1001B92E8 |
| Kirikiroid2_1.3.9_iOS_armv7 | 0xE9874 | 0xE9A28 | 0x1B8992 |

## Common native flow and differences

```text
LoadStorage:
  normalize/open stream; reject missing stream or size < 9
  aligned allocation; read entire stream
  if MDF decode succeeds: release original and select decoded allocation
  return Adopt(buffer, size, filter)
Adopt:
  reject size < 64 or signature != PSB
  construct raw owner/header view; publish via temporary PSBFile holder
  if filter exists: invoke filter; return owner.Refresh(true)
  return true
seed stream:
  x=123456789; y=362436069; z=521288629; w=low32(seed); remaining=0
  for each byte:
    if remaining == 0:
      t = uint32(x XOR (x << 11))
      rotate x,y,z,w
      w = uint32(old_w XOR (old_w >> 19) XOR t XOR (t >> 8))
      remaining = w
    byte XOR= low8(remaining); remaining >>= 8
```

None of the four Adopt functions reads the encryption flag or decodes a header
before constructing its view. Without a filter they do not validate offsets.
The current portable implementation matches that native sequence.

Android arm64 inlines MDF and header refresh; the other targets retain helpers.
Pointers/sizes are 64 bits on arm64 and 32 bits on armv7. libstdc++/libc++ callable
storage and holder cleanup differ. All seed invokers commit RNG state only when
the remaining word is zero, whether expressed with branches or predication.
The arm64 invokers truncate pointer difference to signed 32 bits; armv7 compares
pointer bounds. The added header operation is bounded to 48 bytes and leaves
these native paths intact.

## Observed and unavoidable Web boundary

A real original full-package cold-load/advance probe consistently traps at the
first FindNameIndex memory load. The paused native stack shows a live retained
owner, PSB version 4, encryption flag 1 and encrypted offsets outside the Wasm
heap. Original saving succeeds separately.

The browser cannot execute the game's Windows emoteplayer DLL. Its resource
reaches the reconstructed reader without that platform's header decoding. The
following is an explicit additional Web boundary; the four reference engines
do not automatically decode these modern PC headers.

Three independent original resources validated the bounded operation: version 4
has a 56-byte header. XOR the first encrypted word with the header length to
obtain the first RNG word; invert w XOR (w >> 19) to recover its seed. The stream
above decodes the remaining header. All three Adler-32 checksums match exactly,
including extra offsets and excluding the checksum field. No game identity,
resource name, hash or fixed key selects the operation.

## Local correspondence planned before C++ edits

Preserve signature/minimum-size checks and every owner publication, filter,
refresh and lifetime operation. Immediately before constructing the owner, only
Web builds without an explicit filter may decode flagged version 3/4 headers
in a fixed 56-byte scratch buffer.

Try the bounded first-word lengths required by the native zero-word refill rule.
Accept a unique checksum- and offset-validated decoded header. Only then copy its
header bytes back and clear the header-encryption flag. Preserve the body and
source file. Invalid/ambiguous encrypted headers fail before owner publication.
Explicit filters and non-Web builds keep the native flow above.

A standalone native driver can validate existing original PSB inputs without
linking the engine or creating game fixtures. The public repository PSB fixture
is unencrypted version 2 and covers the unchanged path in CI. The encrypted path
requires the existing private originals plus the real full-package browser
probe. Private inputs must not be committed or fabricated.
