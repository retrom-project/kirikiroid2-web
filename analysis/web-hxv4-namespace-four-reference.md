# Web normalized Hxv4 namespace and native storage boundaries

This is an explicit Web container extension. It does not attribute Hxv4 or
Windows bootstrap execution to the four KiriKiri references. Fresh native MCP
decompilation preceded the implementation; every function below was read to a
completed cursor. Raw transcripts and private archive measurements stay outside
Git in the integration environment's acceptance directory.

## Per-binary mapping

| Function | Android arm64-v8a | Android armabi-v7a | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| TVPOpenArchive | sub_90C074 | sub_6C99F0 | sub_1001D96B8 | sub_1D802E |
| XP3 Create | sub_8FD134 | sub_6C15E8 | sub_1003686B0 | sub_36AA2C |
| XP3 constructor | sub_8FCCC8 | sub_6C14E0 | sub_100368530 | sub_36A81C |
| XP3 Init | sub_8FC1C4 | sub_6C0D38 | sub_100367A94 | sub_369DE8 |
| TVPGetPlacedPath | sub_8EBC80 | sub_6B85DC | sub_1001937AC | sub_1930B0 |
| TVPAddAutoPath | sub_8EB894 | sub_6B83BC | sub_100193484 | sub_192CB0 |
| TVPRemoveAutoPath | sub_8EBA70 | sub_6B84CC | sub_10019360C | sub_192EA8 |
| Storages registration | sub_8EE030 | sub_6B9368 | sub_1001948A4 | sub_194308 |
| addAutoPath wrapper | sub_8EE360 | sub_6B969C | sub_100194BA4 | sub_194710 |
| removeAutoPath wrapper | sub_8EE408 | sub_6B9704 | sub_100194C18 | sub_1947E0 |
| TVPBeforeSystemInit | sub_90E13C | sub_6CACAC | sub_1002A0878 | sub_2A5294 |
| TVPExecuteStartupScript | sub_8E5004 | sub_6B4FCC | sub_100187C10 | sub_185684 |

The filenames are respectively `Kirikiroid2_1.3.9_Android_arm64-v8a.so`,
`Kirikiroid2_1.3.9_Android_armabi-v7a.so`, `Kirikiroid2_1.3.9_iOS_arm64`, and
`Kirikiroid2_1.3.9_iOS_armv7`. Addresses belong only to their columns. The older
Android64 `0x8EB4B4` AutoPath annotation is not the function in this supplied
reference: the fresh registration -> wrapper chain calls `sub_8EB894`.

## Joint native control flow

```text
openArchive(name, normalize):
  create input stream; try ZIP, 7z, TAR, XP3 creators in order
  rewind between misses; release stream when every creator misses

XP3.create(name, optionalStream, normalize):
  create stream when omitted; check XP3 signature/offset
  miss: release only owned stream, return null
  match: construct XP3 archive, whose constructor invokes Init

XP3.Init(stream, offset, normalize):
  find offset when negative; seek to index pointer
  repeat:
    read index flag and size
    flag & 7 == 0: read raw index; == 1: inflate exactly advertised size
    otherwise throw
    enumerate File chunks in source order
    read required info/name, segments and adlr; normalize name if requested
    segment flag & 7 == 0 / 1 selects raw / zlib; otherwise throw
    append archive item and increment count
  while index flag has continue bit
  if normalize: stable-sort items by name
  release index buffer and input stream, including exception cleanup

addAutoPath(path) / removeAutoPath(path):
  hold storage lock; require final archive delimiter, slash or backslash
  normalize path; find exact normalized path in ordered vector
  add: append only when absent; remove: erase only when present
  clear placed-path cache and auto-path table; mark table uninitialized

getPlacedPath(name):
  return touched original-name cache hit
  hold storage lock; normalize name
  if exact current-folder/archive storage exists: cache and return it
  extract basename; rebuild auto-path table when uninitialized
    each ordered archive path: enumerate only sorted prefix's direct children
    each ordered loose path: enumerate through storage-media lister
    later additions replace the same basename's earlier table binding
  return/cache selected path + basename, or empty on miss

beforeSystemInit():
  initialize arguments/data path; apply optional archive delimiter
  append archive delimiter to existing project storage, otherwise slash
  set current directory; then initialize random/memory configuration

executeStartupScript():
  process optional patch.tjs with original error handling
  resolve configured startup name; clear startup-success
  create/destruct preflight text stream; execute that entry once
  set startup-success; process optional AfterStartup.tjs
```

Android64 inlines string/hash/vector ownership operations. Android32 outlines
more container operations and splits 64-bit offsets across registers. Both iOS
targets use libc++ stable-sort buffers and cleanup helpers, unlike Android's
libstdc++ lowering; iOS armv7 also has SjLj/VFP spill scaffolding. Item strides,
virtual-call pointer widths, guard values and refcount lowering differ with ABI.
These differences do not change the listed branches, ordering or defaults.
BeforeSystemInit's memory-option parsing also differs in string ABI lowering;
the extension does not alter that code. No cross-platform byte offset is copied
into the Web implementation.

## Why this is a separate platform boundary

Fresh UTF16, UTF8 and UTF32 byte/string searches for
`CompoundStorageMedia` completed without a match in all four targets. The
independently followed four-target loader/registration chains expose the native
storage APIs above and the ordinary XP3 loader, not a Windows PE bootstrap or its
compound-media class. This absence is based on multiple encodings plus actual
loader and archive call chains, not one source grep.

Normalization authenticates and decodes the source container outside the core,
then retains the entire namespace in an ordinary XP3 `RHx4` index chunk. It
retains actual resource bytes and original physical names. The browser therefore
needs the declared hash lookup algorithms and media salt, not Windows DLL
execution, decryption keys, signature-success stubs, or a plaintext dictionary.

## Local comparison and extension plan

`XP3Archive::Init` retains the native index/File/segment/cleanup sequence. Only
Emscripten reads the optional `RHx4` chunk. Its bounded records bind to verified
physical names **before** the native final item sort; source File indices must
never index the sorted vector. Ordinary XP3 archives take the existing path.

`StorageIntf` keeps the native cache, normalization, exact lookup, ordinary
AutoPath table and miss sequence. Hash resolution runs only with the new media
active, and existing explicit physical archive members retain their identities.
A Web media named `arc` is installed only for
an entry archive carrying the new namespace. It provides `arc://./` as the
default domain, retains all initial-domain directories, and maps only explicitly
added source directories into the root in insertion order. Removing a path
restores the initial domain plus remaining mappings; mounted numeric domains
remain stable. Physical placed paths are actual `archive>syntheticName` URLs.
Unknown plaintext names cannot be invented for enumeration: listings expose
resolvable physical resource identities.

The existing Add/Remove operations still own the ordered native vector for
ordinary fallback paths. The normalized frontend intercepts accepted raw paths
before that vector, owns the separately ordered hash-directory pairs, and clears
the native placed-path cache when those pairs change. It does not pre-mount
every archive or search every directory by basename. The Web bootstrap runs
after native project-directory selection and before the unchanged game entry.
It neither patches that script nor retries an internal Initialize script.

The current unreleased `RHx4` payload is a bounded UTF16LE media salt and a count
followed by 52-byte records: path hash 8, filename hash 32, synthetic ID u32,
archive slot u16, local flags u16, and original File index u32. Path hashing is
SipHash-2-4 with zero key and LE output; basename hashing is unkeyed BLAKE2s-256;
both consume canonical UTF16LE query plus the recovered media salt. Directories
include a trailing slash, except the empty root. Duplicate bindings, unsupported
slots, malformed UTF16, oversized tables and inconsistent physical IDs fail.
This shape is edited in place with its producer; no intermediate PFB protocol
generation or compatibility reader is introduced.

## Supplied bootstrap's mixed-storage boundary

This additional input is a supplied Windows x86 bootstrap, not one of the four
KiriKiri native references. Its fresh registration chain is
`sub_1000FAE0 -> sub_10010760 -> sub_10014F80`. The last function constructs
`TargetChunkParser("Hxv4")`, invokes the XP3 index walker `sub_10014C90`, then
requires an octet payload before authenticated table decoding. The generic
chunk walk `sub_1000DF00` compares only the configured four-byte tag; its target
callback `sub_100153A0` accepts the 14-byte Hx descriptor and reads the external
payload. Ordinary `File` chunks are not converted into a compound hash table.
Without Hx metadata the result remains non-array and logs `table not found`.

The native bootstrap replaces the Storages Add/Remove methods through
`sub_1000AC20`. Add `sub_1000B810` and Remove `sub_1000BB70` both use
`sub_1000B440` to split the **raw** argument, then `sub_1000B950` to call the
script callback with archive source, directory and add/remove flag. The splitter
passes an unqualified directory without `>` as source `">"` (initial domain).
A URI containing `://` and `>` is split at `>` into archive and directory.
Fully qualified loose-directory URIs, and unqualified strings containing `>`,
return member-not-found and retain the original method's fallback. This split
must precede ordinary full-path normalization, which would lose that distinction.

For an existing plain archive the script callback sees the non-array table,
records its empty-domain mapping and returns true. `sub_1000B950` converts that
true value to `TJS_S_OK` (zero); the Add/Remove wrappers return that success
unchanged. Thus this case is consumed, rather than added to the native basename
table. The Web frontend retains the same empty-domain/consumed state for an
existing archive without the normalized namespace. Successful mounted domains
keep stable numeric IDs; failed mounts do not allocate one. Removing an archive
that was never mounted does not mount it as a side effect.

Ordinary loose fallback paths keep the four-reference current-storage-before-
AutoPath priority. A later ordinary File archive is not an authenticated hash
overlay and cannot replace an existing hash-root entry through this native
bootstrap protocol. Supporting another overlay policy would be a separate,
explicit platform extension, not an inferred behavior of the supplied plugin.

Actual complete source archives are the integration inputs. A successful import
alone is not title/story/save/checkpoint acceptance. No new fabricated core
game fixture or synthetic native oracle is introduced for this boundary.
