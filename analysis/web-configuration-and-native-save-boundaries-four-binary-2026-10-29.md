# Web configuration, startup and native save boundaries

These mappings record the four-reference evidence underlying the affected native seams. Fresh native IDA decompiles, literal-byte checks and type repairs preceded implementation. Browser-only additions are described separately in [Web platform boundaries](../docs/web-platform-boundaries.md); they are not attributed to the original binaries.

| Function | Android arm64-v8a | Android armabi-v7a | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| TVPInitScriptEngine | sub_8E1F08 / 0x8e1f08 | sub_6B3930 / 0x6b3930 | sub_1001863C0 / 0x1001863c0 | sub_183A84 / 0x183a84 |
| TVPExecuteStartupScript | sub_8E5004 / 0x8e5004 | sub_6B4FCC / 0x6b4fcc | sub_100187C10 / 0x100187c10 | sub_185684 / 0x185684 |
| WIN32Dialog initializer | sub_60F79C / 0x60f79c | sub_5269C8 / 0x5269c8 | sub_1002A222C / 0x1002a222c | sub_2A7078 / 0x2a7078 |
| Array.saveStruct | sub_982344 / 0x982344 | sub_70FF7C / 0x70ff7c | sub_100150E44 / 0x100150e44 | sub_153020 / 0x153020 |
| Dictionary.saveStruct | sub_9C4B5C / 0x9c4b5c | sub_73715C / 0x73715c | sub_1000A5EBC / 0x1000a5ebc | sub_A4DDC / 0xa4ddc |
| TVPGetPlacedPath | sub_8EBC80 / 0x8ebc80 | sub_6B85DC / 0x6b85dc | sub_1001937AC / 0x1001937ac | sub_1930B0 / 0x1930b0 |

Binary filenames are respectively `Kirikiroid2_1.3.9_Android_arm64-v8a.so`, `Kirikiroid2_1.3.9_Android_armabi-v7a.so`, `Kirikiroid2_1.3.9_iOS_arm64`, and `Kirikiroid2_1.3.9_iOS_armv7`. Each address belongs only to its column.

## Startup and preprocessing

Shared initialization sequence: return if initialized; set initialized; process `-evalcontext=global`, `-unaryaster=compat`, `-debug=yes`, optional `-startup`; construct TJS; install random callback; execute constants; install console and stream callbacks; register built-in classes, including native Bitmap, retaining variant/refcount ownership; install extension/compaction hooks. No branch sets `kirikiriz`; the original default must remain unchanged. The Web opt-in sets that preprocessing value after construction and before the first script compilation.

The four startup functions execute optional `patch.tjs` with its existing error handling, resolve the selected startup name, clear startup success, create/destruct the preflight text stream, execute the selected entry once, set success, log completion, then process optional `AfterStartup.tjs`. There is no retry of a project-internal initialization script after partial entry execution. The local catch-and-retry was removed to restore this sequence and preserve the original failure. ASCII patch-name constructors and UTF-16 entry-name references were checked independently.

All four WIN32Dialog initializers execute the same class declaration containing only `messageBox`, which delegates to `System.inform(message, caption, 2)`. Header/Items declarations, public SDK constants and the event-target model constructor are explicitly Web extensions. They cannot create HWNDs or register native common controls.

## Structured file serialization

Shared sequence: get native instance or fail; Dictionary additionally rejects invalid native state; initialize empty name/mode; accept a string filename when supplied and a non-void mode; select binary if mode contains `b`. A filename selects the configured stream factory; no filename selects a memory writer. Binary writes the eight-byte `KBAD100\0` header. Both branches create the native recursion vector, push objthis, invoke SaveStructuredBinary/Data, then delete the binary stream or Destruct the text stream, including unwind cleanup. File results return the original objthis closure; memory results return octet/string and suppress that object result.

The existing local nonempty-string file branch preserves this factory -> recursion vector -> native serializer -> stream close -> objthis sequence. The Web scope observes that branch without changing its result, exception or file bytes. Only a completed, validated BMP prefix plus an explicit append offset qualifies for native progress publication. Ordinary settings serialization does not qualify. Nested failures invalidate the outer transaction. The existing local omission of the native no-filename memory-return path is independently verified and remains outside this observation change; the observer neither compensates for nor depends on it.

## Storage resolution

Shared placed-path sequence: touch the original-name cache and return a hit; acquire the storage critical section; normalize; return/cache exact existing current-folder or archive storage; otherwise extract basename; rebuild the ordered auto-path table if uninitialized; lookup basename; concatenate selected path, cache and return, or return empty on miss. Rebuild enumerates each archive prefix's direct children using sorted archive entries and balanced archive references, or collects loose-directory children through the storage lister. Later native search paths override matching basenames.

The local native sequence remains intact. An optional Web resource search layer runs before that cache. Its explicitly indexed loose files and individual XP3 members can override explicit archive-member reads; whole base archives are retained for unmatched resources. The browser cannot execute a supplied Windows patch-loader DLL, which is the concrete platform reason for this boundary.

## Differences and local comparison

Android uses libstdc++ helpers and iOS libc++ helpers. Android64 inlines many string/hash/vector operations; Android32 outlines vector insertion and string cleanup; iOS64/iOS32 use libc++ growth/destruction helpers. ARM32/64 differ in register/stack guards, virtual-call widths and atomic reference-count lowering. iOS armv7 has SjLj spill/cleanup scaffolding. These do not change the conditions/defaults above. Array and Dictionary have distinct local memory-text-writer vtables in all four and must not be silently collapsed when reconstructing that separate path. Their Dictionary validity and Array result-temporary differences remain explicit.

`ScriptMgnIntf.cpp` retains the original option branches, constructor, callbacks and class registration; only opt-in Web preprocessing and the corrected one-shot startup are changed. `tjsArray.cpp` and `tjsDictionary.cpp` retain the existing native file-branch sequence and gain an Emscripten-only observation scope. `StorageIntf.cpp` retains the native cache, normalization, existence, auto-path and miss sequence after its optional Web lookup. Non-Web WIN32Dialog initialization is unchanged.

Existing native bridge fixtures cover save transactions, exceptions, nested failure and optional system-data flush behavior. Actual native save and resource files exercise the image classifier and overlay lookup through product acceptance. No separate standalone classifier/overlay oracle fixture exists; none was fabricated. Temporary diagnostic exports and debug linker options are excluded from the delivered build.
