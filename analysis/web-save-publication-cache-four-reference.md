# Web save publication and storage lookup caches

The native lookup sequence was freshly decompiled in all four matching IDBs
before this change:

| Binary | TVPGetPlacedPath | Table initialization flag |
| --- | --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `sub_8EBC80`, `0x8ebc80` | `0x1af0198` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `sub_6B85DC`, `0x6b85dc` | `0x1141fb0` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `sub_1001937AC`, `0x1001937ac` | `0x102527958` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `sub_1930B0`, `0x1930b0` | `0x214cc94` |

Shared pseudocode is unchanged: return a cached original-name placement; lock
storage; normalize and check the exact current path; otherwise extract basename;
build the auto-path table once by enumerating archive/direct-directory entries;
lookup the basename in that table and cache a hit, or return empty. Later
registered paths overwrite earlier basename entries. Android64 inlines string,
hash and table operations more heavily; iOS32 has SjLj/register save scaffolding.
These compiler/ABI differences do not change the cached-directory policy.

The observed failure is a new loose file published after that directory table
was built. The VLFS directory and bytes already contain it, while native
basename lookup still misses and its caller falls back to an archive path.
Existing old files continue resolving, which is why a first save/restart alone
did not expose this second failure.

## Explicit platform boundary

The Web fork's buffered file stream publishes through `TVPWriteDataToFile` and
VLFS overlay close. This browser namespace mutation does not deliver a native
filesystem/deactivation notification to the cached storage layer. The four
references do not contain this VLFS/JavaScript publication boundary. The fix
does not change their shared lookup algorithm, container or precedence rules:
after closing the Web write, invalidate the existing engine storage caches so
the next lookup enumerates the current namespace. Do this before returning to
the native serializer. A short write still returns failure; its actual partial
file remains visible rather than inventing rollback semantics.

The C++ replay executes the production Web publication function with two
unrelated paths. It verifies a newly written file becomes discoverable from an
already cached directory, close precedes invalidation, and short-write failure
is preserved. The private product regression exercises repeated native saves,
new-instance restore and ring reuse without changing scripts or indices.
