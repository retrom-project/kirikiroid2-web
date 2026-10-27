# PackinOne data-pack reader

## Evidence and scope (2026-10-27)

This is an additional PackinOne read capability, not a reconstruction claim
about the Kirikiroid2 1.3.9 built-in Scripts class. The user explicitly approved
using the supplied Windows DLL as supplemental evidence for this interface.
No game name, identity, asset path or allowlist selects the implementation.
The four original targets remain the authority for their existing APIs.

The supplemental PE32 `PackinOne.dll` has SHA-256
`9fe1ebd957247cd5561b8e22b86e12895bb6c03f83d0d0b3b9ce6ed94e58e7d7`.
It was extracted without execution from the existing test package. All native
evidence was obtained with explicit-database `mcp__idalib__*` calls in this
conversation. Inputs and raw native results remain outside Git in the workspace
fixture directory. No original project source was searched for or imported.

## Original four-file mapping

| Reference binary | Scripts constructor | Factory | Global init | loadDataPack |
|---|---|---|---|---|
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0x8e75dc | 0x8e3f34 | 0x8e1f08 | Not registered or located |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0x6b6054 | 0x6b44bc | 0x6b3930 | Not registered or located |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x100189370 | 0x1001872ec | 0x1001863c0 | Not registered or located |
| Kirikiroid2_1.3.9_iOS_armv7 | 0x187224 | 0x184a74 | 0x183a84 | Not registered or located |

Fresh decompilation of all four constructors, factories and initialization
chains gives the same registration set: finalize, Scripts, execStorage,
evalStorage, compileStorage, exec, eval, dump, getTraceString, setCallMissing,
getClassNames and textEncoding. Exact UTF-8/16/32 loadDataPack/saveDataPack
searches returned no matches. All exact UTF-16 Scripts references were also
followed: JSON adds evalJSON/evalJSONStorage/saveJSON/toJSONString; scriptsEx
adds rehash/eval/setEvalErrorLog. This bounded search does not exclude an
external game plugin or runtime script supplying a new interface. There is no
four-platform loadDataPack pseudocode to merge, and no platform difference to
silently choose between. The supplemental DLL positively resolves its origin.

Array.loadStruct was separately decompiled in all four references
(respectively 0x981cac, 0x70fb40, 0x1001507ec, 0x15298c). Each accepts the
KBAD100 binary header and uses the original binary serializer; none has a text
fallback. The suspected text fallback is not part of this change.

## Supplemental mapping

Every address in this section belongs only to the Windows `PackinOne.dll` above.

| Function / RTTI-backed component | Address |
|---|---|
| Scripts data-pack registration | 0x10053550 |
| Scripts.loadDataPack callback | 0x10053180 |
| Options constructor / read properties / write properties | 0x1004abd0 / 0x1004cdc0 / 0x1004a3e0 |
| Variant-to-IV byte conversion | 0x1004c530 |
| StorageReader constructor | 0x10046ee0 |
| Carrier recognition | 0x1004eea0 |
| Header decode / options copy | 0x1004d070 / 0x1004c760 |
| Read orchestration | 0x100530e0 / 0x10053030 / 0x10052f80 |
| Stream filter chain construction | 0x10050f10 |
| Crypt mode selection | 0x10050d40 |
| BasicCryptFilter constructor | 0x1004fa40 |
| BLAKE2s initialization / keyed parameter init / update / final | 0x10041970 / 0x100419e0 / 0x100438b0 / 0x10043970 |
| ChaCha state setup / core / extended block fill | 0x10048030 / 0x1004fc40 / 0x1004ff50 |
| XXH32 | 0x10055bb0 |
| StreamCryptReaderWriter construction / read | 0x1004da80 / 0x1004daf0 |
| LZ4 read/write mode selection | 0x1004e760 |
| LZ4StreamBase constructor / configure | 0x1004e480 / 0x1004e510 |
| LZ4DecompressStream read | 0x1004e5a0 |
| ArchiveStreamBuf constructor | 0x1004a520 |
| BinaryStreamForRead constructor | 0x10050190 |
| Variant archive and trailing check | 0x100526f0 |

## Pseudocode before implementation

```
loadDataPack(source, optional options):
    default options: false flags; compress/cryptmode/seed zero; empty IVs/comment
    if optional argument is Object: read its properties
    String source -> StorageReader; Octet source -> StringReader
    other source -> TJS_E_INVALIDPARAM
    clear result if requested
    recognize raw TJS header or replace source with PNG/JPG chunk reader
    parse 16-byte header and optional IV
    retain caller nocheck; copy caller outeriv separately; overwrite parsed options
    construct crypt reader if cryptmode != 0
    construct LZ4 reader around the selected reader if compress != 0
    configure buffered input: compressed ? 4100 : 4096 bytes
    decode root Variant and check type bytes + trailing check
    options.checkfail = !check_result
    if !nocheck && checkfail: throw read error
    copy root into result if requested; write output options for Object argument
    release local Variant, filters and source on all exits
```

Input option properties use PropGet flag 0x400 (MEMBERMUSTEXIST), which invokes
property getters. Output properties use PropSet flag 0x200 (MEMBERENSURE).
Read helpers ignore negative status and retain the corresponding default;
property exceptions still propagate. A review caught an initial local use of
IGNOREPROP (0x800); it was corrected against the native helpers before commit.

Header: exact `TJS/` (little endian) or `TJS\\` (big endian), compression
`n`/`4`, exact `s0\0`, u32 seed, u16 crypt mode, u16 IV length, then IV bytes.
Numeric header fields follow the selected byte order. Caller IV/compression/
seed fields do not override the header. A nonempty outeriv replaces the header
IV as the cryptographic input; it is not concatenated. Output iv remains the
header IV. String IV properties are raw UTF-16 including the terminating code
unit; Octet properties use their exact bytes; other types produce empty bytes.

Crypt modes (rounds, block expansion factor): 1=(8,16), 2=(12,8), 3=(20,4),
4=(8,1), 5=(12,1), 6=(20,1). Key is BLAKE2s-256(iv, key=seedLE4). Nonce is
XXH32(iv, seed) followed by seed; counter starts at zero. Native ChaCha state
stores bitwise-complemented words and complements them for rounds/output.
Each fill generates one ChaCha block, then extends each preceding group of
16 words with xorshift32 (13,17,5). Zero substitutes nonceLow XOR seed, or seed
when equal and nonzero, or 0xffffffff otherwise. Counter advances once per
expanded block, not once per generated 64 bytes. Preserve streaming position
across arbitrary read boundaries.

LZ4 reads a little-endian u16 compressed length, then exactly that many bytes;
reject zero, too large, short and invalid blocks. Use LZ4_decompress_safe_continue
and alternating output buffers so previous-block history remains alive. This
length prefix stays little endian even with a big-endian Variant archive.

`nocheck` does **not** disable the type checker: the reference initializes it
to 1, always computes validity and writes checkfail. It only suppresses the
final check-failure exception. Malformed data still throws.

The Variant archive mapping, pseudocode and per-branch behavior are recorded
separately in `datapack-variant-archive.md`. Carrier wrappers are documented
separately after their complete native call-chain inspection.

## Local implementation and platform boundary

`datapack/Reader.*` keeps source ownership, filters, header options and buffered
input as separate steps. `datapack/Crypt.*` keeps keyed hash, complemented
ChaCha state, block expansion and streaming cursor. `VariantArchive.*` uses
real TJS Array/Dictionary dispatch and reference counting, not a JSON or STL
replacement. `DataPack.cpp` attaches only loadDataPack to the PackinOne module;
it does not advertise unimplemented saveDataPack/thumbnail APIs or change the
existing core Scripts class or save layout.

The native DLL uses Windows COM IStream behind StorageReader. The portable
implementation uses the engine's tTJSBinaryStream at that explicit platform
boundary; Web game and lazy XP3 reads continue through the existing storage
interface. Windows SimpleBinder's std::exception-to-TJS boundary is represented
by the existing NCB raw callback and TJS exception conversion.

## Current evidence coverage

All 42 existing private PBD assets decoded through the diagnostic translation;
all use raw little-endian TJS/4s0, cryptmode 1 and no embedded IV. They cover
68 LZ4 blocks and 182742 decoded bytes. An independent parser consumed the
first 5314-byte payload, validated all 214 type/check pairs and the trailing
0x00fcc970 check, and recovered a 19-item TJS array with positive dimensions.
The production C++ header/filter/buffer chain passed ASan and UBSan on all 42
assets: all 182742 output bytes matched independently decoded payloads.
The production TJS binding then loaded all 42 original Octets in the browser;
every array element, dictionary field, scalar value and output option matched
the independently decoded objects. The storage-name path also returned the
same positive dimensions and layer count. Normal saved-game playback restored
the previously absent character. These checks used existing private content,
kept outside this repository.

An existing asset with its final ciphertext byte flipped exercises a check
failure: default policy throws, nocheck returns the parsed object and sets
checkfail=1. A truncated prefix throws malformed data; a wrong source type
returns the TJS invalid-argument error. These are transformations of the same
existing sample, not a newly invented archive or game fixture.

Existing PBDs do not cover nonempty embedded/outer IV, modes 2-6, the big-endian
archive, PNG/JPG carriers, or payload void/null/octet/real values. Their code
has the documented native evidence, but no runtime coverage is claimed for
those branches. The original PFB's base Layer bitmap may remain 1x1 while the
game draws its populated AffineSourceStand; its dimensions are not a reliable
sprite-rendering assertion. The test observes the real source dimensions,
populated PSD layers, and the actual displayed frame.
