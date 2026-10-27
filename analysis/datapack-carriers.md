# PackinOne data-pack carrier readers

## Evidence scope and platform mapping (2026-10-27)

This is the user-authorized Windows supplemental implementation of the new
PackinOne Scripts.loadDataPack capability. It does not establish a common
implementation in the four Kirikiroid2 mobile references. All DLL addresses
below belong only to PE32 PackinOne.dll, imagebase 0x10000000, SHA-256
9fe1ebd957247cd5561b8e22b86e12895bb6c03f83d0d0b3b9ce6ed94e58e7d7.

The four-reference API investigation and native call records are linked from
[datapack-reader.md](datapack-reader.md). Its bounded registration/string/xref
search found no loadDataPack registration or originating carrier call chain:

| Reference binary | Scripts constructor | Carrier mapping state |
| --- | --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0x8e75dc | No loadDataPack-originating chain located |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0x6b6054 | No loadDataPack-originating chain located |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x100189370 | No loadDataPack-originating chain located |
| Kirikiroid2_1.3.9_iOS_armv7 | 0x187224 | No loadDataPack-originating chain located |

This does not claim that no image-related classes exist elsewhere in those
images. There is no four-platform loadDataPack carrier pseudocode to merge.
Windows is an explicit additional capability boundary, not a substitute for
the original reference contracts.

This pass used native mcp__idalib calls against the adopted parent-owned
bakoom_packinone session and did not execute the DLL. Full native return values
and the detailed Chinese evidence ledger are retained outside source control in
the workspace fixture/bakoom-kirikiri-windows/evidence directory:
wrappers-native-20261027.json and wrappers-analysis-20261027.md.
No original source repository was searched for or imported.

## Windows function mapping

| Function / RTTI-backed class | Address | Evidence |
| --- | --- | --- |
| Carrier recognition and raw magic | 0x1004eea0 | W1 |
| PNG probe / wrapping | 0x10043d20 / 0x10047af0 | W4 / W2 |
| PNG Read / destructor | 0x10043ce0 / 0x10043c70 | W-0x10043ce0 / W-0x10043c70 |
| PNG scalar deleting destructor | 0x10043e40 | W-0x10043e40 |
| JPG probe / wrapping | 0x1004e9e0 / 0x1004ece0 | W5 / W3 |
| StreamEscapeCharFilter constructor | 0x1004d700 | W-0x1004d700 |
| Next APP11 segment | 0x1004e940 | W-0x1004e940 |
| Escape filter fill / decode / Read | 0x1004d8a0 / 0x1004d770 / 0x1004f4f0 | W-0x1004d8a0 / W-0x1004d770 / W-0x1004f4f0 |
| Escape filter SetBlockSize / vector reserve | 0x1004d870 / 0x1004d7e0 | W-0x1004d870; follow-up reserve decompile |
| JPG destructor / scalar deleting destructor | 0x1004e890 / 0x1004e920 | W-0x1004e890 / W-0x1004e920 |
| Unsupported Seek | 0x10040ec0 | W-0x10040ec0 |
| PNG signature / vtable | 0x10078a30 / 0x10078b8c | W-vtables |
| JPG vtable | 0x1007a578 | W-vtables |

## Pseudocode established before implementation

The native Reader virtual +4 method returns a byte count. Native seek origin 0
is relative-current and 1 is absolute, as jointly demonstrated by tell/restore
and payload skipping. Local Reader uses cstdio SEEK_CUR and SEEK_SET instead.

```
UnwrapCarrier(slot):
    if slot is empty: return false
    saved = source.Seek(0, CURRENT)
    prefix = source.Read(4 bytes)
    if saved >= 0: source.Seek(saved, ABSOLUTE)  // ignore restore result
    if prefix was short: return false
    if prefix == "TJS/" or "TJS\\": return true
    if prefix == 89 50 4e 47:
        require exact full PNG signature at current position
        while exact Read(8-byte chunk header):
            length = big-endian u32 header[0..4]
            reject length with bit 31 set
            if header[4..8] == "tjSs":
                move source ownership into PNGChunkStreamReader(length)
                return true
            skip = sign-extend-32(length + u32(4))  // payload + CRC
            if source.Seek(skip, CURRENT) < 0: return false
        return false
    if prefix != ff d8 ff e0: return false
    require exact Read(ff d8)
    pair = source.Read(2 bytes)
    if exact:
        loop:
            if pair[0] != ff: return false
            if pair[1] == da: break  // only SOS marker consumed
            require exact Read(2-byte big-endian length), length >= 2
            if source.Seek(length - 2, CURRENT) < 0: return false
            if next marker Read(2) is short: break
    allocate temporary byte buffer of 0x4000
    scan repeated source.Read(up to 0x3fff):
        locate each ff with memchr, inspect following byte
        if ff is the final byte: read one additional byte; fail on zero read
        ff d9: return false
        ff eb: seek back to that ff, ignoring seek result;
               move source ownership into JPGChunkStreamReader; return true
        all other following values, including 00: continue scanning
    return false

PNGChunkStreamReader.Read(dst, requested):
    n = min(requested, remaining)
    actual = source.Read(dst, n)
    if actual == 0: return 0
    remaining -= n
    return n  // a positive short read is treated as a full n-byte read

StreamEscapeCharFilter.SetBlockSize(size):
    source.SetBlockSize(size)
    buffer.reserve(4 * size)  // reserve: preserve logical size

JPGChunkStreamReader.ReadNextChunk(rawLength):
    require exact Read(4-byte header) and first two bytes ff eb
    rawLength = big-endian u16 header[2..4]
    if rawLength < 2: return false
    rawLength -= 2
    buffer.resize(rawLength, 0)
    source.Read(buffer.begin(), rawLength)  // deliberately ignore count
    return true

StreamEscapeCharFilter.Fill(requested):
    if ReadNextChunk(requested) == false: clear buffer; return 0
    capture end at buffer.begin() + requested
    for each input byte up to captured end:
        copy current byte to output
        if current byte == ff:
            if following byte is beyond captured end:
                read one additional source byte
                if read returned < 1: substitute 00
                ++requested  // end remains unchanged
            DecodeEscape returns ff only when following == 00, else -1
            if DecodeEscape >= 0: consume following byte; overwrite output
    shrink buffer to decoded output bytes
    return decoded count

StreamEscapeCharFilter.Read(dst, requested):
    repeatedly drain decoded buffer
    if buffer empty: Fill(remaining request); stop if zero
    copy min(buffer.size, remaining request)
    empty whole-consumed buffer or compact leftover bytes to its beginning
    return actual total copied
```

PNG accepts only the first tjSs payload, not concatenated chunks. Its successful
probe leaves the source at the payload start and the target CRC unconsumed.
It neither checks CRC nor terminates specially at IEND. The signed length+4
wrap and each negative-seek check are kept, rather than introducing new bounds.

JPEG dispatch is exactly ff d8 ff e0, not any SOI or Exif prefix. Before SOS,
all markers including APP11 are skipped using their length. Scanning starts
immediately after the two SOS marker bytes, without parsing the SOS length or
scan header. The byte stream must then contain a target ff eb before ff d9.
The wrapper subsequently joins only contiguous APP11 segments. Every segment
has a u16 big-endian size including its two length bytes but excluding marker
bytes; there are no sequence numbers, total length, CRC, or payload read-count
checks. Non-APP11 headers stop a Read after consuming that header.

An explicit length of 2 passes the native segment length gate; the native MSVC
checked begin() dereference can still report an invalid parameter for an empty
vector. The portable Reader returns no decoded bytes in that case. This is an
unverified debug-runtime boundary, not an added format rejection.

Both wrappers have unsupported Seek returning -1. PNG SetBlockSize is a no-op.
JPG forwards SetBlockSize, then reserves four times that count. Their owning
destructors release source; JPG does so before destroying its byte buffer.
The public local Reader is read-only, so the DLL's Write methods are outside
this interface: PNG Write returns 0, while the escape filter Write inserts 00
after each ff. No writer or thumbnail extraction API is advertised.

## Local implementation correspondence and platform differences

The implementation is cpp/plugins/datapack/Carrier.cpp:

- UnwrapCarrier preserves the magic recognition and tell/restore order, then
  replaces the unique_ptr only after a positive probe.
- ProbePng follows 10043D20; PNGChunkStreamReader keeps source and remaining,
  uses 10043CE0's nonzero-short-read accounting, and owns source to destruction.
- ProbeJpg follows 1004E9E0 including the partial-header transition into scanning,
  the boundary-byte read and the unchecked back-seek.
- JPGChunkStreamReader inherits StreamEscapeCharFilter with ff/00 fields;
  ReadNextChunk corresponds to 1004E940, Fill to 1004D8A0, DecodeEscape to
  1004D770, Read to 1004F4F0, SetBlockSize to 1004D870/1004D7E0.
- The internal byte vector preserves fill, per-byte in-place decoding, partial
  read compaction and resize/reserve. No hard-coded PE32 object padding or
  checked-iterator proxy layout is copied into portable source.
- When a raw chunk ends with ff, local code creates one valid scratch byte
  before the native additional Read. This preserves bytes consumed, zero-read
  fallback and captured input end while avoiding an out-of-object write.
- Native 1004F4F0 calls memcpy to compact possibly overlapping byte ranges.
  Local code uses memmove for that same compaction. This represents the
  observed x86 byte result without exporting undefined C++ overlap behavior.
- Native ownership flag is always set true on this load path. Local owning
  unique_ptr expresses the same lifetime, and JPG explicitly resets source
  before base buffer destruction. Allocation failure follows normal throwing
  C++ new; the native compiler-generated null-new branch is not an alternate
  format behavior.
- Native underlying StreamReaderWriter origin integers are translated at the
  explicit Reader interface boundary to cstdio SEEK_* values.

## Validation limits

All existing PBD assets observed by the parent investigation are raw TJS/4s0.
They do not exercise PNG/JPG wrappers. No new thumbnail fixture, malformed file,
or generated container has been created. No Windows DLL execution or browser
carrier runtime claim is made. Source compilation and integration validation
must be recorded after implementation; existing product data can validate the
raw path only. The unchecked CRC, ignored restore/back-seek results, positive
PNG short-read accounting and ignored JPG segment read count remain deliberate
native behavior, not an assurance of strong input validation.

After implementation, both host C++17 and Emscripten 6.0.9 syntax checks passed
with -Wall -Wextra. The new source and this analysis also pass whitespace checks.
Full engine integration and product validation remain with the parent task.
