# Web native-plugin storage names

The browser cannot load a game's Windows DLL/TPM machine code. It resolves the
storage reference to the same named implementation compiled into the core.
For DLL/TPM references only, the Web boundary removes either path separator's
directory prefix and maps the TPM suffix to DLL. The existing NCB loader owns
ASCII case folding, dependency registration and duplicate suppression. Other
names retain their complete keys. This rule depends only on the plugin storage
syntax; it has no game-name, content-hash or package-identity conditions.

## Four-reference mapping checked before implementation

| Reference binary | `Plugins.link` | Outer loader | Lowercase loader |
| --- | --- | --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `0x9086a0` | `0x548e24` | `0x701de8` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `0x6c7e58` | `0x4a9648` | `0x5ba8e8` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `0x1003f2210` | `0x100287b38` | `0x10029fde4` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `0x3d9884` | `0x28a8a4` | `0x2a48fc` |

All twelve functions were decompiled through native IDA MCP in this change's
implementation session, with the corresponding database explicitly selected.
The four common flows are:

```text
link(args): require at least one argument; convert args[0] to string
            call loader; ignore its bool; return success
loader(name): lowercase copied name
              if registered or absent from internal map: return false
              run Pre/Class/Post registrar lists
              publish registered key; return true
```

No reference strips paths in the loader. Android and iOS differ in ABI and list
implementation, and iOS rewrites discovered TPM suffixes during autoload. The
Web normalization is an explicit platform boundary, not a claim about native
reference behavior. `ncbind` and the non-Web caller remain unchanged.

`TVPLoadPlugin`'s Web branch applies the storage-to-module conversion, then calls
the same loader. Native builds keep the complete input. The unit regression
executes the actual name converter with directory references, both separators,
case variations and unrelated suffixes; the old identity mapping fails it.
