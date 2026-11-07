# WindowEx native-instance constructor (four-reference)

## Function mapping

| Binary | Creating getter | Constructor body | GetHWND | regist |
| --- | --- | --- | --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | `sub_627800` (inlined ctor, `new(0xC0)`) | inlined | `sub_612DB8` | `sub_627AE8` |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | `sub_530E50` (`new(0xA4)`) | `sub_530F30` | `sub_528464` | `sub_5310A0` |
| Kirikiroid2_1.3.9_iOS_arm64 | `sub_10021D0F4` (inlined ctor, `new(0xC0)`) | inlined | `sub_100212E40` | `sub_10021D3A0` |
| Kirikiroid2_1.3.9_iOS_armv7 | `sub_21C54C` (inlined ctor, `new(0xA4)`) | inlined | `sub_210DF8` | `sub_21C87C` |

## Common pseudocode

```
self = obj;
menuex = sysMenuModified = sysMenuModMap = 0;
cachedHWND = GetHWND(obj);          // PropGet(L"HWND")
sysMenu = externalIcon = 0;
<flag bytes> = 0;
ovbmp = 0;                          // last pointer field (a64/i64 +184, a32/i32 +160)
regist(true);
setMessageHookAll(false);           // memset(bitHooks, 0, 0x80); enableWinMsgHook = false
```

## Per-binary differences

- arm64 (both): zeroes six flag bytes (+48 WORD, +50, +52 WORD, +54). armv7 (both): zeroes three
  flag bytes (+28 WORD, +30). Which bool names they map to is not proven; the local
  `has*` flags keep being written by `registerExEvent` before any read, so they're unchanged.
- armv7 a32 zeroes +4..+26 with `memset(0x17)` and then writes +16 = GetHWND; i32 stores fields one by one. Same source.

## Local deviation fixed

Before the fix, the local constructor never initialized `ovbmp`, `cachedHWND`, `sysMenu` or `externalIcon`. `~WindowEx` →
`deleteOverlayBitmap()` deleted a garbage `ovbmp` and corrupted the dlmalloc heap. The corruption then surfaced as
`emscripten_builtin_free` traps during `Window.close()` → `ncbInstanceAdaptor<WindowEx>::Invalidate` on native exit.
