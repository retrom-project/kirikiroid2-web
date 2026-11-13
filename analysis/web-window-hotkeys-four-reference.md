# Web Window hotkey boundary

## Evidence collected before implementation

Fresh native IDA MCP decompiles used each database explicitly. The four
complete Window registration functions contain no `registerHotKey` member.
Independent ASCII and UTF-16LE searches for `registerHotKey` and
`onHotKeyPressed` were negative, with `onKeyDown` and `onCloseQuery` as positive
controls. Truncated UTF-16 registration literals were read as bytes and repaired
in each IDB before refreshing the decompiles.

| Reference binary | Window registration | Key down | Key up |
| --- | --- | --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0x8790f8 | 0x87801c, shared tail of Deliver at 0xaa79d0 | 0x87818c, shared tail of Deliver at 0xaa7ce4 |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0x675108 | 0x67446c | 0x674594 |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x1000c7f78 | 0x1000c7670 | 0x1000c77fc |
| Kirikiroid2_1.3.9_iOS_armv7 | 0xc5fb8 | 0xc56a0 | 0xc5870 |

Android64 IDA groups the two window bodies with their tail-calling input-event
Deliver functions. Instruction queries confirm independent stack prologues at
the listed window addresses; Deliver reads source/key/shift and branches there.
Decompiles of those complete shared tails and disassembly supply the mapping.
The native tooling could not split those function chunks; no binary bytes were
changed.

Shared native pseudocode:

```text
registerWindow(): construct native class; register ordinary methods/events/properties
onKeyDownOrUp(key, shift):
    if !CanDeliverEvents(): return
    if Owner:
        args = [integer(key), integer(shift)]
        PostEvent(Owner, Owner, onKeyDownOrUp, tag=0, IMMEDIATE, args)
    if DrawDevice: DrawDevice.onKeyDownOrUp(key, shift)
```

The 32-bit variants use 12-byte variants; the 64-bit variants use 20-byte
variants. Android64 folds delivery into the tail described above. Android uses
libstdc++ string cleanup, iOS uses libc++ cleanup, and iOS armv7 has SjLj spills.
Atomic static-string guards and stack canaries differ by compiler/ABI. There is
no native hotkey branch or registration to reconstruct in these mobile builds.

`WindowIntf.cpp` already implements that native key event sequence. It keeps
the eligibility check, integer arguments, immediate script event, and following
DrawDevice call unchanged. New registration state and dispatch are enclosed
in `__EMSCRIPTEN__`; non-Web class registration and event flow stay identical.

## Supplemental Windows plugin protocol

The supplied original WindowEx DLL is PE32, image base 0x10000000, SHA-256
`159e358a0f720b23100e692519a8e4e4b40c1295be1a9e5ca1eb774b04caffc7`.
It is supplemental API evidence, not a replacement for the four mobile
references. Native IDA decompiled registration at 0x10009a60, the binding at
0x1001a0e0, message dispatch at 0x10018b20, and the two-argument event helper at
0x10014c20 completely.

```text
registerHotKey(id [, key [, shift]]):
    require at least one argument; require valid window native instance
    convert id to unsigned; reject id > 0xbfff
    if key supplied and key != 0:
        map TJS shift/alt/control/Windows bits to OS modifiers
        if shift supplied: disable repeat unless ssRepeat is set
        return bool(RegisterHotKey(window, id, modifiers, key))
    else: return bool(UnregisterHotKey(window, id))
WM_HOTKEY: call window.onHotKeyPressed(id, TJS modifier bits)
```

TJS modifier bits are shift=1, alt=2, control=4, Windows=0x100; repeat=0x80.
Omitting the third argument leaves OS repeat enabled. The native callback's
return controls Windows message handling, which has no equivalent OS message in
the browser.

## Explicit browser boundary and local implementation

A browser cannot call Windows RegisterHotKey or receive background OS hotkeys.
The Web extension registers shortcuts on the actual virtual Window and responds
only to keyboard events delivered to it. Registration replaces an ID, removal
reports whether that ID existed, modifiers match exactly, and key-up resets
repeat state. Per-window owned state is destroyed with its native instance.
Matching callbacks use the existing TJS event queue and retain both arguments.
No default `onHotKeyPressed` member is fabricated: scripts can discover and
install their own callback. Ordinary keyboard delivery remains intact.

The implementation uses the supplied native method's argument/default/error
rules, with real state and event delivery replacing the unavailable OS calls.
There are no game-name, archive-hash, filename, script-rewrite or save-rewrite
conditions. Existing complete private packages will supply runtime validation;
no new game fixture is manufactured.
