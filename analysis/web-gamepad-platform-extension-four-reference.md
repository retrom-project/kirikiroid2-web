# Web gamepad.dll platform extension and native contract (2026-10-30)

## Evidence boundary

The four authoritative mobile references do not expose the named `gamepad.dll`,
`GamepadPort`, `Gamepad`, `gpTouchDown` or `gpButtonA` TJS contract. Fresh native
IDA function-name, ASCII, UTF-16LE and UTF-32LE searches cross-check this absence;
the UTF-16LE `xp3filter.dll` positive control resolves in all four. iOS system
`GCGamepad` Objective-C names are not this TJS plugin. The new implementation is
an explicitly Web-only platform extension, not a reconstruction of an existing
mobile gamepad plugin. No generic loader or game script is patched.

| Reference | Module wrapper | Internal loader | Internal registration | Plugins.link |
| --- | --- | --- | --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0x548E24 | 0x701DE8 | 0x548D04 | 0x9086A0 |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0x4A9648 | 0x5BA8E8 | 0x4A9598 | 0x6C7E58 |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x100287B38 | 0x10029FDE4 | 0x100287ACC | 0x1003F2210 |
| Kirikiroid2_1.3.9_iOS_armv7 | 0x28A8A4 | 0x2A48FC | 0x28A7DC | 0x3D9884 |

All sixteen functions were freshly decompiled completely before implementation.
The shared pseudocode is:

```text
internalRegistration():
    AllRegist(Pre); AllRegist(Class); AllRegist(Post)
    LoadModule("xp3filter.dll")
LoadModule(input):
    key = lowercaseAscii(copy(input))
    if registeredSet.contains(key): return false
    module = internalMap.find(key)
    if missing: return false
    for line in [Pre, Class, Post]:
        for registrar in module[line]: registrar.Regist()
    registeredSet.insert(key)
    return true
Plugins.link(args):
    if argc < 1: return BADPARAMCOUNT
    LoadModule(string(args[0]))       # ignore bool
    return OK
```

Android64 unrolls the three registrar walks; Android32 and both iOS targets
loop three times. Android libstdc++ and iOS libc++ list linkage, pointer width,
stack canaries and temporary-string cleanup differ; the public control flow
agrees. These differences are not new fallback or external-PE execution paths.

The existing `cpp/core/plugin/{PluginImpl,ncbind}.cpp` already follows that
shared flow and remains unchanged. Repeated differently scoped source searches,
plugin source-list review and actual project bytecode establish that the gap is
a missing statically registered class, not a failure to discover a PE file.

## Additional supplied Windows protocol evidence

The supplied `gamepad.dll` is PE32 i386, image base `0x10000000`, SHA-256
`2f9fcaad5cd1e880b590303e1a231212c91b7fdb120068f8cc6ffb04600423ed`.
Its bytes were read from a real archive entry and passed that entry's original
Adler32 before native IDA analysis. It is supplemental protocol evidence and
does not replace the four-reference absence/loader evidence.

`gamepad.dll!V2Link@0x1001FF80` executes the literal `gp*` constants expression,
then creates and registers `GamepadPort` and `Gamepad` in that order, with
normal TJS native-class/native-instance wrappers. Port registration is
`gamepad.dll!GamepadPort_register@0x1001FCB0`; controller registration is
`gamepad.dll!Gamepad_register@0x1001EE50` (705 lines, fully paged).
The 79 distinct registered method/getter/setter callbacks were all freshly
decompiled completely, including access-denied getter/setter callbacks.

| Registered member | gamepad.dll callback addresses (method or getter / setter) |
| --- | --- |
| GamepadPort.finalize | 0x1001da10 |
| GamepadPort.GamepadPort | 0x1001d9b0 |
| GamepadPort.initialize | 0x1001fc40 |
| GamepadPort.getController | 0x1001eb50 |
| GamepadPort.count | 0x1001e8d0 / 0x1001d9a0 |
| Gamepad.finalize | 0x1001d990 |
| Gamepad.Gamepad | 0x1001e830 |
| Gamepad.update | 0x1001eb10 |
| Gamepad.name | 0x1001e9c0 / 0x1001d980 |
| Gamepad.type | 0x1001e7c0 / 0x1001d970 |
| Gamepad.leftTrigger | 0x1001e760 / 0x1001d960 |
| Gamepad.rightTrigger | 0x1001e700 / 0x1001d950 |
| Gamepad.leftThumbStickX | 0x1001e6a0 / 0x1001d940 |
| Gamepad.leftThumbStickY | 0x1001e640 / 0x1001d930 |
| Gamepad.rightThumbStickX | 0x1001e5e0 / 0x1001d920 |
| Gamepad.rightThumbStickY | 0x1001e580 / 0x1001d910 |
| Gamepad.leftVibration | 0x1001d900 / 0x1001e500 |
| Gamepad.rightVibration | 0x1001d8f0 / 0x1001e480 |
| Gamepad.keyState | 0x1001e410 / 0x1001d8e0 |
| Gamepad.analogLeftUpCount | 0x1001e3b0 / 0x1001d8d0 |
| Gamepad.analogLeftDownCount | 0x1001e350 / 0x1001d8c0 |
| Gamepad.analogLeftLeftCount | 0x1001e2f0 / 0x1001d8b0 |
| Gamepad.analogLeftRightCount | 0x1001e290 / 0x1001d8a0 |
| Gamepad.analogRightUpCount | 0x1001e230 / 0x1001d890 |
| Gamepad.analogRightDownCount | 0x1001e1d0 / 0x1001d880 |
| Gamepad.analogRightLeftCount | 0x1001e170 / 0x1001d870 |
| Gamepad.analogRightRightCount | 0x1001e110 / 0x1001d860 |
| Gamepad.degitalUpCount | 0x1001e0b0 / 0x1001d850 |
| Gamepad.degitalDownCount | 0x1001e050 / 0x1001d840 |
| Gamepad.degitalLeftCount | 0x1001dff0 / 0x1001d830 |
| Gamepad.degitalRightCount | 0x1001df90 / 0x1001d820 |
| Gamepad.buttonStartCount | 0x1001df30 / 0x1001d810 |
| Gamepad.buttonBackCount | 0x1001ded0 / 0x1001d800 |
| Gamepad.buttonLeftThumbCount | 0x1001de70 / 0x1001d7f0 |
| Gamepad.buttonRightThumbCount | 0x1001de10 / 0x1001d7e0 |
| Gamepad.buttonLeftShoulderCount | 0x1001dda0 / 0x1001d7d0 |
| Gamepad.buttonLeftTriggerCount | 0x1001dd30 / 0x1001d7c0 |
| Gamepad.buttonRightShoulderCount | 0x1001dcc0 / 0x1001d7b0 |
| Gamepad.buttonRightTriggerCount | 0x1001dc50 / 0x1001d7a0 |
| Gamepad.buttonACount | 0x1001dbe0 / 0x1001d790 |
| Gamepad.buttonBCount | 0x1001db70 / 0x1001d780 |
| Gamepad.buttonXCount | 0x1001db00 / 0x1001d770 |
| Gamepad.buttonYCount | 0x1001da90 / 0x1001d760 |

Other relevant `gamepad.dll` functions:

| Role | Function/address |
| --- | --- |
| NI factories | sub_1001EDE0@0x1001EDE0, sub_1001EDB0@0x1001EDB0 |
| Port constructor and destruction | sub_10007C80@0x10007C80, sub_10007CE0@0x10007CE0 |
| Native Construct / controller Invalidate / destruction | sub_10007D50@0x10007D50, sub_10007D60@0x10007D60, sub_10007B70@0x10007B70 |
| Port manager initialize | sub_10026BA0@0x10026BA0 |
| Dense list lookup | sub_10026080@0x10026080 |
| Clear owned device list | sub_10026140@0x10026140 |
| Initialize enumerated devices / remove failures | sub_10026310@0x10026310 |
| Manager destruction | sub_10026230@0x10026230 |
| NI state initial zeroing | sub_100071A0@0x100071A0 |
| Device poll, assemble snapshot, update counts | sub_1001E930@0x1001E930 |
| Direction count update | sub_10020860@0x10020860 |
| Full state and 24 count update | sub_10020910@0x10020910 |
| XInput poll/disconnection | sub_10020320@0x10020320 |
| XInput raw key mask | sub_10020310@0x10020310 |
| Left/right triggers | sub_10020290@0x10020290, sub_10020270@0x10020270 |
| Left stick X/Y | sub_100204B0@0x100204B0, sub_10020460@0x10020460 |
| Right stick X/Y | sub_10020410@0x10020410, sub_100203C0@0x100203C0 |
| Write-only vibration amplitudes | sub_10007A90@0x10007A90, sub_10007AE0@0x10007AE0 |

The update path's complete 51-instruction assembly proves six separate floating
point getters are stored into a local snapshot before the key mask is read.
The complete 398-instruction count-update assembly disambiguates direction
argument ordering: X updates left/right; Y updates down/up because XInput
positive Y points upward. It is not inferred from property names.

## Protocol pseudocode before implementation

```text
Port.initialize(hwnd):
    require one argument and coerce it to integer
    clear the manager's owned std::list of devices
    enumerate actual devices; initialize each; remove failed devices
    leave script result void
Port.count:
    return current stored device-list size
Port.getController(index):
    require one argument and a result slot
    lookup dense list element by unsigned index; missing -> INVALIDPARAM (-1003)
    create a new global.Gamepad(nativeDevicePointer)
    return this fresh script object

Gamepad.Construct:
    OK, no work
Gamepad.Gamepad(pointer):
    require one argument; store borrowed nativeDevice pointer
Gamepad.Invalidate:
    nativeDevice = null
Gamepad.update():
    if nativeDevice:
        nativeDevice.update()
        s = {leftX,leftY,rightX,rightY,leftTrigger,rightTrigger,keyState}
        update direction / trigger / digital counts from s
        copy s into this instance's previous-snapshot prefix
    leave script result void
value getters:
    return current device value if nativeDevice exists; otherwise leave void
count getters:
    return this script controller instance's own counters
vibration:
    getter -> ACCESSDENIED
    setter -> clamp amplitude to [0,1], truncate *65535; queue device output
all other setters:
    ACCESSDENIED
finalize script methods:
    OK, no work; actual ownership is in native instance / manager destructors
```

For each digital or trigger condition, counts saturate at signed 32-bit bounds:

```text
pressed:  count = old <= 0 ? 1 : min(INT_MAX, old + 1)
released: count = old > 0 ? 0 : max(INT_MIN, old - 1)
axis(value, negativeCount, positiveCount):
    advance(negativeCount, value < 0)
    advance(positiveCount, value > 0)
```

A nonzero trigger advances its counter; the plugin does not apply the game's
0.6 threshold. XInput axes use signed 16-bit values and deadzones 7849 (left)
and 8689 (right), normalized separately by the positive and negative residual
ranges. Triggers are unsigned byte / 255. The TJS constants use the actual
XInput digital masks and preserve the original spelling `degital*Count`.

## Local implementation mapping and platform differences

The Web extension uses the same two TJS native classes, owned device list and
per-script-instance count state. A browser device backend replaces Windows
XInput/DirectInput. All identity selection is by actual browser device
index/id, never by a game identity. Only browser `mapping === "standard"`
devices are exposed as the XInput-shaped contract; raw DirectInput axis
reassignment, DirectInput force-feedback and Windows HWND are unavailable.

Browser standard axes are clamped/quantized to signed 16-bit input, with Y
inverted, then passed through the native deadzone/normalization steps. Buttons
are mapped through the standard layout to the literal native masks. An absent,
unexposed, nonstandard or disconnected device is not synthesized into a
connected controller.

Vibration is best effort in the supplied Windows plugin. Its TJS setters
`0x1001E500` and `0x1001E480` invoke void device setters and then return OK;
`0x10007A90` and `0x10007AE0` only clamp/quantize and queue the 16-bit amplitudes.
The backend submits changed output during `0x10020320`; its result changes an
internal connection flag, not the TJS setter's result. Returning NOTIMPL when
browser hardware lacks an actuator would introduce a script exception absent
from that native contract, including when a script writes zero to stop output.
The Web backend must queue the same amplitudes and invoke actual available
browser `playEffect`/`reset` methods. Missing actuators and asynchronous hardware
failures are best effort and do not claim that vibration physically occurred.
Finite browser effects are refreshed while native updates request nonzero
output and stopped when device ownership or runtime lifetime ends.

The browser supplies device connection events rather than Windows messages;
a Web platform bridge must deliver real connection changes through the
existing Window message hook contract. It must not call a game's re-detection
function by name. Native sampling uses the Provider-filtered Gamepad API;
enumeration observes metadata without consuming its stateful host chord
filter. Actual sampling uses one complete filtered native snapshot held by the
Runtime cursor for the current browser frame. Every controller reads that same
group, so polling one controller cannot consume another controller's short
Start/Select pulse. The cursor invalidates the snapshot on each animation frame
and every policy, focus, suspension or ownership reset; snapshots never bypass
those gates. This is an internal Web platform sampling bridge, not a new TJS or
persistent protocol. Standalone builds without the Runtime bridge read the
browser Gamepad API directly.

Enumeration refresh and notification observation have separate roles: explicit
Port initialization refreshes the device list without consuming a topology
change owed to other ports/windows. The continuous observer owns advancement
of the notification baseline. The first enumeration establishes its baseline
so an already attached controller is not fabricated into a new connection.

A controller used by the script owns its input, so the adapter releases
and suspends the alternative gamepad mouse cursor. Games that do not instantiate
the plugin retain the existing cursor path. Cleanup releases that ownership.

No game's startup arguments, script contents, archives, entry names, IDs or
package checksums are matched in compiled implementation code. Validation must
separately report keyboard/mouse story progress and actual controller coverage.
There is no matching Linux native test preset; do not invent native unit passes.

## WindowEx notification subscriptions: fresh four-target evidence

The same round of native IDA calls additionally resolves the real
`setMessageHook` registration and its complete callback body in each reference.
Raw call output and readiness probes are retained in the private acceptance
directory as `native-four-windowex-hook.json`.

| Role | Android64 | Android32 | iOS64 | iOS32 |
| --- | --- | --- | --- | --- |
| WindowEx registration | `0x60F8C8` | `0x526A90` | `0x10021118C` | `0x20F150` |
| setMessageHook callback | `0x6117B8` | `0x5279CC` | `0x1002123D0` | `0x2102C4` |
| single-message helper | inlined in callback | `0x528628` | `0x10021309C` | `0x210FFE` |
| UTF-16LE method literal | `0x14CE6D4` | `0xD80466` | `0x101964E7E` | `0x175722A` |

All four implement 1024 subscription bits as 32 unsigned 32-bit words plus an
aggregate enabled flag. The bitset starts at offset 56 on 64-bit targets and
32 on 32-bit targets; the aggregate flag is at 54 and 30 respectively. Android64
inlines the helper and vectorizes the all-message fill, while the other builds
call the helper and use scalar or smaller vector fills. These are ABI/compiler
differences, not different message-subscription behavior.

Joint pseudocode, obtained before changing the local WindowEx implementation:

```text
setMessageHook(on = false, optional message):
    native instance missing -> ACCESSDENIED
    if message supplied:
        convert string through notification dictionary, otherwise to integer
        require 0 <= message < 1024, otherwise FAIL
        set or clear its subscription bit
        enabled = any of the 32 words is nonzero
    else:
        fill all 32 words with on ? 0xffffffff : 0
        enabled = on
    result = enabled; return OK
```

Local comparison: `bitHooks[32]` already exists, but `setMessageHookOnel` ignores
both arguments and `setMessageHookAll` changes only the aggregate flag. Restore
the observed bitset transitions and return value in place. Use explicit 32-bit
words to preserve the recovered layout independently of host `unsigned long`.

All four UTF-16LE searches find the setter and find no
`onWindowsMessageHook` literal. The supplied mobile references therefore do not
provide a deliverable Windows message callback. The browser notification route
is an explicit Web extension, not claimed mobile behavior. The actual supplied
script subscribes to message 537 and checks callback arguments 1 and 2 against
537 and 7 before scheduling its own re-enumeration; the native plugin still
owns enumeration. The Web bridge only posts this documented device-topology
change notification after the actual connected standard-device metadata changes,
only to windows whose bitset subscribes to that message, and on the engine's
continuous-event thread. It never invokes a game's re-enumeration function.
