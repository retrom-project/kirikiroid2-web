# Web engine argument source and native option initialization

The four matching binaries and IDBs were independently copied into this PFB's
ignored `reference/binaries/`. All eight copies matched source SHA-256 and
stable source stat before opening. This conversation freshly queried each IDB
through native idalib, verified its module/imagebase, and followed the UTF-16
`getArgument` registration to its native implementation. Raw tool results and
input digests are retained in the PFB's ignored acceptance evidence.

| Reference binary | System.getArgument | TVPGetCommandLine role | Full argument initialization | Config parsing / append |
|---|---|---|---|---|
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | sub_911C3C@0x911c3c | sub_90DE30@0x90de30 | sub_90E72C@0x90e72c | sub_910158@0x910158 |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | sub_6CCE04@0x6cce04 | sub_6CABCC@0x6cabcc | sub_6CB004@0x6cb004 | sub_6CBC68@0x6cbc68 |
| Kirikiroid2_1.3.9_iOS_arm64 | sub_100099250@0x100099250 | sub_1002A06BC@0x1002a06bc | sub_1002A0CA4@0x1002a0ca4 | sub_1002A1D38@0x1002a1d38 |
| Kirikiroid2_1.3.9_iOS_armv7 | sub_97858@0x97858 | sub_2A50CC@0x2a50cc | sub_2A5760@0x2a5760 | sub_2A6AE4@0x2a6ae4 |

Every mapped function above has fresh decompile evidence. The Android arm64
config append was paged through its final return. `PushAllCommandlineArguments`
has no named function in the four function queries; the four complete init
call chains contain no argv producer, and their config containers are null.
This is a bounded absence in these mobile initialization paths, not a claim
about every possible platform or an alleged original Windows implementation.

Common native flow, preserving intermediate option stock and ordering:

```text
System.getArgument(result, argc, argv):
  if argc < 1: return bad-parameter-count
  if result:
    name = ttstr(argv[0])
    if not GetCommandLine(name, result): clear result to Void

GetCommandLine(name, out):
  initialize full argument stock/data path once
  scan the vector in its existing order
  if name-prefix matches and next code unit is '=': return suffix, true
  if name-prefix matches and next code unit is NUL: return "yes", true
  otherwise continue; return false when no exact option name matches

InitializeFullArguments():
  if initialized: return
  set initialized before recursive option queries
  construct three null option-layer pointers
  append default/embedded layers (null here)
  construct native data path from empty configured path and executable path
  clear argument stock
  append user, default, embedded layers in that priority order
  release temporary layers and variants
  normalize data path, set log path, increment argument generation

AppendConfig(options):
  if null: return
  for each narrow string in original order:
    skip if first byte is ';'
    option = "-" + ttstr(narrow string)
    find first '='
    if absent: append "=yes"
    otherwise preserve name and '=', then parse an initially quoted TJS string
      or retain the remaining unquoted value
    append ttstr to the existing option vector
```

The four implementations agree on the conditions and defaults. Android's
libstdc++ uses pointer-based COW narrow strings; iOS uses libc++ SSO strings
(24/12-byte objects on its two architectures). String refcount/cleanup and
vector clear code differ by ABI/compiler. iOS armv7 also has decompiler-visible
callee-saved floating registers. These are not additional option behavior.
UTF-16 argument-name bytes were read in all four files. IDA's truncated ANSI
string display in the iOS constructors remains unsuitable as a name oracle;
the complete bytes and registration target establish the names. Type annotations
were corrected and callers recompiled without changing input bytes.

Local implementation comparison before the behavioral change:

- `TVPProgramArguments` already uses `std::vector<ttstr>`, and
  `TVPParseCommandLineOne` already has the native first-equals, quoted-value,
  bare-flag and empty-value steps. Both stay in place.
- `TVPGetCommandLine` already scans exact names from the front; it stays in
  place. Supplying arguments with repeated names therefore preserves the first
  value's priority.
- The full local initialization clears the vector and invokes its empty
  command-line producer twice around that boundary. Writing values once before
  initialization would lose them. Web now retains raw argv independently and
  replays it at both existing producer calls, ahead of config layers.
- `platforms/web/main.cpp` receives UTF-8 argc/argv but previously ignored it.
  The Web platform now copies argv[1..] before the application runs, preventing
  use after Emscripten main's argument storage is released. Conversion uses the
  engine's existing narrow-string constructor, as in native config parsing.
- The source getter and capability export are explicitly Web platform glue:
  mobile binaries cannot provide a browser Host's arguments, and the browser
  cannot execute a supplied Windows launcher. No mobile behavior or shared
  argument parsing is replaced. Non-Web builds retain the empty producer.
- Provider configuration bounds and validates the explicit string array,
  preserves order, rejects NUL/control characters, and reserves project entry
  and persistent data path options to their existing platform contracts. It
  selects no argument value from title, hash, package identity or language files.

The exact optional Web capability is `krkr2_host_engine_arguments_abi() == 1`.
An explicitly configured nonempty argument list requires that capability before
main/script startup. Empty default arguments continue to work without it.
This is an addition to the selected development ABI, not a new generation.

Validation uses the existing runtime adapter/configuration fixture and real
project startup. No synthetic C++/TJS game fixture was created. The real package
already provides a deterministic before/after observation of its native
`System.getArgument` branch; passing that branch is not end-to-end gameplay.
