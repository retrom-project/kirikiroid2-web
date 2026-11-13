# Host window-close request

The Web Host was exporting files and disposing the instance without sending
the native window close event. A game's `finalize` can write system data after
its save slot was already written. Calling only a KAG metadata method does not
close the native window and cannot run a non-KAG window's finalizer.

## Four reference mappings

All four binaries and their matching readable IDBs were checked in this
conversation. Each function below was decompiled with its own native IDA
session before changing C++.

| Binary | BaseWindow OnClose | WindowLayer OnCloseQuery |
| --- | --- | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `sub_8767E4`, `0x8767e4` | `0xaa7d08` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `sub_673224`, `0x673224` | `0x7bc580` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `sub_1000C6828`, `0x1000c6828` | `sub_100E47FB0`, `0x100e47fb0` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `sub_C4604`, `0xc4604` | `sub_C55800`, `0xc55800` |

The UTF-16 event literal is `onCloseQuery` in each file, independently checked
with raw bytes (Android64 `0x150c042`, Android32 `0xdb41d8`, iOS64
`0x101959c4a` / `0x10197db86`, iOS32 `0x174bfae` / `0x176ff38`).

Shared pseudocode:

```text
BaseWindow.OnClose:
    if !CanDeliverEvents() or !Owner: return
    post immediate Owner.onCloseQuery(true)

WindowLayer.OnCloseQuery:
    if breathing: return false
    if no native instance, modal result not 0/cancel, or no owner: return true
    if ProgramClosing:
        CanCloseWork = true
        post immediate Owner.onCloseQuery(true)
        draw scene once
        return CanCloseWork
    if native instance:
        enqueue OnCloseInputEvent(native instance)
    Closing = true
    return false
```

There is no forced acceptance in the user-close branch. Android arm64 uses
bit tests for booleans; 32-bit builds compare to 1, and iOS64 tests nonzero.
Pointer sizes, exception guards, stack canaries and saved floating-point
registers differ. The event, argument, conditions and deferred input path
agree. No ABI offsets are introduced into compiled code.

## Local correspondence and platform boundary

`WindowIntf.cpp` already implements the shared BaseWindow flow, and
`MainScene.cpp` implements the deferred user-close path. The new Web export
only validates startup, drained frame, absence of a save call/flush and an
existing main window, then enqueues that existing `tTVPOnCloseInputEvent`.
It does not call a game-named object, finalizer or serializer, invalidate a
window, or report acceptance. Refusal and native confirmation remain script
events. The Host resumes normal ticks and input while confirmation is pending.

The native references cannot provide browser Promise/Module or Host snapshot
coordination. That is the explicit Web lifetime boundary: after accepted close
reports native termination, drain the original tick (including finalizer I/O),
flush remaining supported system data, detach observers, perform native
teardown, export the complete overlay, then release Module/VLFS. Failed writes
or finalization must not publish an old final snapshot. Ordinary checkpoint
exports never request window close.
