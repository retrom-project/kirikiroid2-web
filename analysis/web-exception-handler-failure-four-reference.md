# Web exception-handler failure boundary

The browser host reports a fatal script exception and returns to the caller so
it can dispose the runtime. A game's exception handler can itself throw (for
example while constructing a native diagnostic window). That secondary error
must not hide the original startup failure or continue through a second closure
release and an unassigned bool result.

## Fresh four-reference evidence

The native IDA MCP session for this change read all twelve functions below,
including completed disassembly for the middle overload in each file.
Addresses are scoped to the named binary.

| Binary | Script exception | Script error with trace | Base TJS error |
| --- | --- | --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0x8e6294 | 0x8e6f54 | 0x8e7278 |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0x6b5748 | 0x6b5c60 | 0x6b5e40 |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x100188688 | 0x100188da8 | 0x100189078 |
| Kirikiroid2_1.3.9_iOS_armv7 | 0x186338 | 0x186b84 | 0x186ec4 |

Getter mapping is respectively 0x8e6518, 0x6b58b4, 0x1001888c4 and 0x1865f8.
UTF16 exceptionHandler searches, getter references and all three callers were
followed independently in each database. The joint normal flow is:

```
zero the Object/ObjThis closure
if script engine or System.exceptionHandler is unavailable: return false
script exception: copy its existing value
script error: construct exception object from message and trace
base error: construct exception object from message only
call the closure with exactly one exception argument
convert its result to bool; destroy temporary variants
release Object, and ObjThis when present, once
return the handler result
```

Android64 inlines variant truth conversion; the other three use a helper.
Android32 uses ARM EHABI, iOS32 has SjLj landing pads and VFP spills, and the
64-bit targets use their platform unwind tables. Pointer sizes, string ownership
lowering and temporary layouts differ. No ABI offsets enter the local code.
The Android64 complete disassembly also contains catch cleanup: 0x8e7184 and
0x8e71c8 start catches, release the closure, call the secondary-error display at
0x8e71c0/0x8e720c, then reach normal closure cleanup. The other three decompiler
bodies exclude their out-of-line EH regions; this note does not infer their
catch behavior from Android64 or claim identical native termination behavior.

## Explicit Web boundary and local comparison

The native normal flow and all non-Web error paths stay unchanged. The local
Web error display uses TVPWebReportScriptException, which intentionally returns
when a host listener accepts the fatal event. Thus a catch cannot fall through
to ordinary closure cleanup or return the handler result: no such result was
assigned when the handler threw. After the catch's existing release, Web logs
the secondary message, reports the original typed error (preserving its script,
line and trace), and returns true to mark that fatal failure as consumed.

This is browser-host error reporting, not a native plugin emulation or a new
protocol. It introduces no game identity, no script rewrite, no retry, no
success fallback, and does not turn an unsupported plugin into a playable game.

The existing private complete-package browser reproduction covers the real
System.exceptionHandler failure chain. Before the change the host receives the
diagnostic-window failure; after the change it must receive the original
missing-plugin member failure. The original package remains unchanged.
