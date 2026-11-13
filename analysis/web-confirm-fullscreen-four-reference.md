# Web confirmation inside fullscreen

The Web two-button path used `window.confirm`, which presents browser chrome
and can leave fullscreen. The replacement keeps the result-bearing native call
and waits for an in-document modal through the existing JSPI platform boundary.

## Fresh native mapping (2026-11-13)

All four matching IDBs were opened through native IDA MCP and checked with
`server_health` before decompilation. Every callback and platform helper below
was decompiled in this change, including the continuation pages.

| Binary | System.inform callback | String/vector adapter | Platform message box |
| --- | --- | --- | --- |
| Android arm64-v8a | 0x9111c8 | 0x922258 | 0x921f78 |
| Android armabi-v7a | 0x6cc914 | 0x6d4dec | 0x6d4bf8 |
| iOS arm64 | 0x100098b5c | 0x100066728 | 0x1000669e8 |
| iOS armv7 | 0x970a8 | 0x64100 | 0x643cc |

The callback's shared flow is:

```
require message; convert message to ttstr
convert supplied non-void caption, otherwise use Information
if button argument is absent/void:
    show one-button message; clear result
else:
    convert Array members, or construct OK/Cancel from the numeric count
    call platform message box and wait for selection
    publish the selected integer index; destroy owned strings/containers
```

The adapter owns converted UTF-8 strings and a vector of their character
pointers until the platform call returns. Both Android helpers locate
`KR2Activity.ShowMessageBox` through JNI, send caption/message/button strings,
and wait for the shared selected index to leave its pending value of -2.
Timed waits keep the native surface responsive. JNI lookup failure returns -1.
Both iOS helpers build a `UIAlertView`, add buttons in supplied order, show it,
then obtain `MessageBoxEventRecevier.getSelectedIndex`.

Android uses libstdc++ strings/deques and iOS uses libc++ strings/deques;
pointer width, string storage, variant strides (20/12 bytes), and waiting
mechanisms differ. Their common contract waits for a real selected index.
The iOS decompiler still abbreviates some UTF-16 literals through old helper
prototypes; the complete Information/OK literals were independently checked in
[the System confirmation analysis](web-system-confirm-four-reference.md).

## Local correspondence and unavoidable Web boundary

System.inform conversion and System.confirm's boolean mapping remain intact.
Only Web's two-button presentation changes. Blocking the browser's main event
loop would prevent a DOM button from receiving input, so `web_confirm` becomes
an `EM_ASYNC_JS` import: copy text/caption to JavaScript strings, await the
core's modal, and return its selected index. Existing JSPI startup/tick entries
retain the synchronous TJS-facing result semantics.

The core supplies a styled HTML dialog inside the game document. This is an
explicit browser implementation, not a reconstruction of JNI/UIAlertView UI.
It neither requests nor exits fullscreen. Long text scrolls; mouse, keyboard
and a standard gamepad select an answer. Input belongs to the modal until
controls return to neutral. Teardown dismisses pending UI without fabricating
an affirmative result. Runtime hooks coordinate its cursor/startup input and
cancel outstanding dialogs before native teardown drains.
The dialog rejects with `KIRIKIRI_DIALOG_ABORTED` during teardown. The frame
scheduler accepts that cancellation only after Host stop has begun, just as it
owns content-read cancellation; cancellation during ordinary play remains an
error. The production scheduler regression covers both orderings.

The current Cocos multi-button Web fallback discards its asynchronous callback
and immediately returns zero; it cannot supply this result-bearing boundary.
No game identity, modified script, implicit consent, or script retry is used.
The existing production-EM_JS VM test seam checks waiting and both index
results; real full-package browser checks cover fullscreen and original callers.
