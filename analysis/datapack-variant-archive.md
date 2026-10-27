# DataPack Variant archive reconstruction

This interface uses supplemental Windows evidence explicitly authorized for this
task on 2026-10-27. It is not attributed to a shared implementation in the four
mobile references.

## Evidence boundary

The Android arm64/armv7 and iOS arm64/armv7 Scripts registration constructors
were separately decompiled in this session. Their complete built-in surface,
factory and exact-Scripts-key native extension paths have no loadDataPack method:

| Reference | Scripts constructor | API status |
|---|---|---|
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0x8e75dc | not registered in examined native surface |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0x6b6054 | not registered in examined native surface |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x100189370 | not registered in examined native surface |
| Kirikiroid2_1.3.9_iOS_armv7 | 0x187224 | not registered in examined native surface |

The supplemental DLL is the game-supplied Windows PackinOne.dll, SHA-256
9fe1ebd957247cd5561b8e22b86e12895bb6c03f83d0d0b3b9ce6ed94e58e7d7.
All remaining addresses in this document refer only to that DLL, base 0x10000000.
Native MCP replies, corrected UTF-16 annotations, exact pseudocode and existing
asset validation are retained under the ignored workspace-root path
fixture/bakoom-kirikiri-windows/evidence/agent-payload-*.
The independent four-image registration report is under
fixture/kirikiroid2-1.3.9/evidence/agent-scripts-findings.md.

## Native mapping and pseudocode before implementation

| Local function/class | DLL evidence | Responsibility |
|---|---|---|
| ReadVariantArchive | 0x100526f0 | root, final unsigned footer, return validity |
| DefaultTypeByteChecker | RTTI vtable 0x1007a0f4 | stateful type-check protocol |
| GetTypeCheck | 0x100471f0 | advance for nonzero tag, reuse for zero |
| CheckType | 0x100471c0 | retain mismatch as status=-1 |
| AdvanceThree | 0x10047240 | three transitions before footer comparison |
| BinaryStreamForRead::ReadByte | 0x100508a0 | one-byte checked read |
| ReadInt32 / ReadUInt32 | 0x10050950 / 0x10051350 | four-byte checked read and optional reversal |
| ReadInt64 | 0x100506e0 / 0x10046a60 | eight bytes, optional reversal |
| ReadReal | 0x100507a0 | eight IEEE bits, optional reversal |
| CheckerByteArchive::GetChecker | 0x10001b90; RTTI 0x1007a294 | attached checker accessor |
| ReadType | 0x100515c0 | always read tag and check byte |
| ReadString | 0x10051620 | signed code-unit count + raw UTF-16 |
| ReadBytes | 0x10051420 | unsigned byte count + 4096-byte chunks |
| ReadArray | 0x100516c0 | count property and recursive indexed insertion |
| GetDictionaryClass / CreateDictionary | 0x100418f0 / 0x10047510 | retained class cache + CreateNew(count) |
| ReadDictionary | 0x10051ee0 | direct string keys and recursive values |
| ReadVariant | 0x10051b30 | exact eight-tag dispatcher |
| LogObject | 0x100415c0 / 0x10041730 | object description + TVPAddLog |
| Malformed | 0x1004bb00 | runtime_error("malformed data") |

Checker seed state:
x=byte0(seed)^byte3(seed), y=byte1(seed), z=byte2(seed), status=1.
For each transition:
t=uint8(x^(2*x)); x=y; y=z;
z=uint8(t^z^((z^(t>>2))>>3)).
readType reads [tag,actualCheck]. Zero tag reuses z; nonzero advances. A
mismatch sets status=-1 and parsing continues. The verifier's boolean does not
short-circuit the dispatcher.

ReadVariant clears its destination then dispatches:
0=void; 1=null dispatch object; 2=TJSString; 3=octet; 4=signed int64;
5=real; 0x81=Array; 0xc1=Dictionary; every other tag throws malformed data.
There is no object-reference ID branch or lookup in this complete reader chain.

ReadString reads signed int32 n, clears the TJSString, and for n>0 allocates n
UTF-16 code units and reads exactly 2*n bytes. It does not individually reverse
UTF-16 code units. Allocation failure is bad_alloc. A nonpositive string count
consumes no code units. ReadBytes uses an unsigned count and constructs/appends
counted std::string chunks of at most 4096 bytes.

For an Array, assign a real TJS closure to the destination before population.
Set its count with flag 0x400 (MEMBERMUSTEXIST). Recursively insert elements with
0xa00 (MEMBERENSURE|IGNOREPROP); do not substitute an STL container. The
temporary value is explicitly cleared before the recursive reader clears it
again. The native error-prefix pointer advances one UTF-16 code unit per item
(0x1005194a, independently disassembled); retain that boundary behavior.

For a Dictionary, retain the classout reference from TJSCreateDictionaryObject,
release the temporary factory instance, and call the class's CreateNew with
the count Variant. Read each key as a direct string, each value recursively,
then PropSet with 0xa00. Assign the resulting closure to the caller only after
population. A negative count returns null from this factory and the dispatcher
creates an empty Dictionary and logs Invalid Dictionary item counts.
Callback status failures log and continue. Exceptions are not swallowed.

Both container insertion paths use 0xa00, without HIDDENMEMBER (0x1000).
The cached Dictionary class is runtime state, not a payload reference table.
Its helper's null-argument release arm is present, but native xrefs show no
caller of that arm and no access to its global outside the helper. Retain the
class reference rather than inventing a different per-call lifecycle.

After root parsing, read unsigned32 footer; advance the checker three times;
expected=(x<<16)|(y<<8)|z, optionally byte-reversed for bigendian.
Return status>=0 && footer==expected. The reader does not require EOF.
Malformed short reads/unknown tags throw runtime_error("malformed data").
A false final check retains the parsed root; the caller controls policy.

## Configuration correction and platform boundary

nocheck belongs to loadDataPack's final exception policy, not the archive
checker. The parent independently decompiled the native options/checkfail flow.
The local function therefore has no nocheck parameter and always initializes
checker status to 1.

The DLL's selected ArchiveStreamBuf and crypto/compression filters are supplied
by the surrounding loader. This module accepts the already-filtered std::istream
and preserves the checker-attached archive and real TJS object operations.
The DLL's compression field controls stream buffering; it is not the numeric
endian flag. Storage, filters, registration and final exception policy are
outside this translation unit.

## Existing asset coverage

Before implementation, an independent analysis parser consumed all 42 existing
decrypted/decompressed game payloads: 182742 bytes, 619 strings, 6055 int64s,
42 arrays and 661 dictionaries. Every tag check and final footer matched, with
no trailing bytes. No new fixture was constructed.

One 5314-byte payload has root end 5310, 19 array entries, 18 layer records,
dimensions 910x1365, and expected/actual footer 0x00fcc970.
The complete values remain in ignored evidence rather than this repository.

Existing assets do not exercise void, null object, octets, real, bigendian,
malformed input or callback failures. Those paths have native static evidence;
no runtime validation of them is claimed. The exact negative Array.count
setter result is delegated to the TJS implementation.

## Local implementation correspondence

The implementation follows the mapping and ordering above. The following line
references are to cpp/plugins/datapack/VariantArchive.cpp:

| Local lines | Correspondence |
|---|---|
| 18-20 | The same malformed-data exception text/type |
| 22-31, 115-140 | Four/eight-byte reversal, integer bit preservation and real bit copy |
| 33-83 | Separate checker with two virtual operations, initial status=1, seed state and three-step footer advance |
| 85-154 | Stream reader, fail/bad checks and checker-attached archive accessor |
| 158-167 | Copy log message, append object description, release it, then TVPAddLog |
| 169-176 | Two byte reads; checker result remains deferred instead of throwing immediately |
| 178-187 | Clear string, signed count, real TJS buffer allocation and raw UTF-16 read |
| 189-201 | Unsigned octet length and counted std::string chunks of at most 4096 bytes |
| 203-222 | Retained classout cache, temporary factory-instance release and native nullable cleanup arm |
| 224-239 | Negative count returns null; real Dictionary-class CreateNew(count), logged callback failure |
| 243-262 | Existing count property, recursive indexed insertion, repeated Clear and advancing error-prefix pointer |
| 264-284 | Create Dictionary first, direct TJSString keys, recursive values and exact PropSet flags |
| 286-337 | Eight-tag switch, destination Clear, real TJS closures and assignment-before/after-population ordering |
| 341-352 | Root, footer, three checker transitions, endian-adjusted expected value and final boolean |

The Array and Dictionary temporary Variants retain both Object and ObjThis;
their original factory reference is released at the same publication stage.
Dictionary class state remains borrowed by closures and retained by the cache.
No STL replacement for a TJS container, alternate grammar or compatibility
version is introduced.

## Implementation validation

The new translation unit passed Emscripten 6.0.9 syntax compilation using the
existing Web release compile_commands.json entry for a plugin source, with
the input replaced by VariantArchive.cpp and -fsyntax-only -Wall -Wextra.
The compiler emitted existing TJS-header warnings, with none in this new file.
The source/evidence boundaries and whitespace were checked separately.

The 42-payload parser validation above preceded this implementation. It proves
that the independently recovered grammar/check transitions fit actual input;
it is not a substitute for executing the compiled TJS implementation. Full Web
candidate and game integration validation is owned by the parent change.
