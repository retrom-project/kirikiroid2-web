# windowEx System.getMonitorInfo — four-reference restoration

## Evidence collected before implementation (2026-10-30)

The copied binary/IDB identities were checked against the independent reference
copies. Native IDA sessions remaining-core-0 through remaining-core-3 were used;
this is a source restoration, not a game-name dispatch or Windows emulation.

| Binary | UTF-16 registration key | Registration | Callback | Fill monitor dictionary | Set rectangle | Parse four coordinates |
| --- | --- | --- | --- | --- | --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0x14cfda6 | 0x623694 | 0x623718 | 0x6233b4 | 0x623510 / 0x612ea8 | 0x6231cc |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0xd8191c | 0x52f188 | 0x52f1b8 | 0x52ef90 | 0x52f098 / 0x5284cc | 0x52ef5c |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x1019668ca | 0x10021abe4 | 0x10021ac14 | 0x10021a878 | 0x10021a97c / 0x100212eb4 | 0x10021a814 |
| Kirikiroid2_1.3.9_iOS_armv7 | 0x1758c76 | 0x219b14 | 0x219b50 | 0x219698 | 0x2197e8 / 0x210ec0 | 0x219660 |

All listed callbacks, fills, rectangle setters, and coordinate parsers were
decompiled in this conversation. The four UTF-16 keys were found independently,
xrefs led to the registration functions, and the raw bytes confirm
`getMonitorInfo`. Some Hex-Rays registration arguments still display the first
ANSI character after correcting their UTF-16 data type; that rendering is not
the authority for the member name. Raw UTF-16 keys `name`, `primary`, `monitor`,
`work`, `x`, `y`, `w`, `h` were also checked (Android arm64 suffix sharing was
resolved with disassembly before reading the suffix addresses).

## Shared control flow and differences

```text
switch parameter count:
  0: no selection input
  2: require parameter[1] to be a non-null object;
     reject only if its IsInstanceOf(Window) call returns zero;
     extract HWND using existing WindowEx helper
  3: convert parameter[0] and parameter[1] to integers; discard coordinates
  5: convert parameter[1..4] into x,y,w,h;
     form right=x+w, bottom=y+h; discard selection rectangle
  otherwise: return BADPARAMCOUNT
clear result
allocate a TJS dictionary accessor
if valid:
  name = "default monitor"; primary = true
  monitor.left = monitor.top = 0
  TVPGetScreenSize(0, monitor.right, monitor.bottom)
  work = monitor
  allocate separate dictionaries for monitor and work
  set x=left, y=top, w=right-left, h=bottom-top
  attach each through a TJS variant to the outer dictionary
  return outer dictionary through a TJS object variant
return S_OK
```

The first parameter of the two/five-argument forms is not consumed; there is no
boolean-driven branching. The three-argument form converts its first two
parameters, as shown in every binary. `IsInstanceOf` is tested for nonzero,
not equality to TJS_S_TRUE: TJS_S_FALSE is also nonzero, and changing this check
would change reference behavior. Failed dictionary allocation leaves a cleared
result with S_OK. Rectangle helper failures do not alter the fill helper's true
return. The fill helper's monitor handle argument is unused in all four.

Android arm64 inlines variant-to-number conversions; the other three call their
conversion helpers. Android arm64 has a stack canary. iOS armv7 includes
floating-point register spill artifacts. Pointer widths, TJS variant layouts,
NCB accessor vtables and inlined reference counting differ by ABI/compiler;
the switch, default status codes, dictionary structure and screen query agree.
No semantic platform difference was found in these functions.

## Web result-pointer boundary

The native NCB FuncCall wrappers were independently decompiled at
Android arm64 `0x9f3bf8`, Android armv7 `0x75268c`, iOS arm64
`0x10004e35c`, and iOS armv7 `0x4d164`. Each conditionally clears a non-null
result, then passes the original pointer unchanged to the callback. None
provides a temporary result. Thus the callbacks' unconditional Clear is also
an original null-result defect, not a missing local reconstruction step.

A legal unused-result TJS statement can supply null. WebAssembly has writable
linear memory at low addresses rather than a native protected null page, so
allowing this defect into Web can silently corrupt engine memory. After the
original argument validation, the Web path substitutes a stack TJS variant
when result is null. All original dictionary operations and lifetimes still
run, and the discarded result is destroyed normally. This is an explicit Web
boundary guard, not a step claimed to exist in the four native functions.
Non-Web behavior is unchanged.

## Local comparison and implementation mapping

Previously `System::getMonitorInfo` returned S_OK without assigning a value for
every call. Its callers therefore received void and failed when reading a
rectangle. Restore the shared switch in that same callback, using existing
`WindowEx::GetHWND`, `tTVPRect`, `ncbDictionaryAccessor` and TJS variants.
Keep parsing/conversion before clearing the result. `setMonitorRect` preserves
the inner dictionary/accessor lifetime; `fillMonitorInfo` preserves the native
default-monitor query and two distinct rectangle dictionaries. This does not
enumerate browser desktop monitors: the existing TVPGetScreenSize platform
boundary supplies the core's logical screen dimensions, as it already does
for the native references. No game scripts or content are modified.

The private real-game acceptance covers the two-argument call. No synthetic
game or fixture is introduced; the remaining overloads are checked against the
four decompilations and compiled with the same implementation.
