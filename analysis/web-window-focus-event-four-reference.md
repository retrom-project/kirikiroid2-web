# Web window focus events: four-reference evidence

The browser needs an explicit focus-to-window-event boundary. This is a Web
platform extension, not a recovered mobile call to a missing function. No
script field, input plugin, game identity or resource name selects the behavior.

## Fresh native evidence

All functions below were independently decompiled through native IDA in this
round on 2026-10-30, completely paged. The copied reference IDBs were health
checked with the expected module, base and Hex-Rays readiness. Raw tool results
remain in ignored acceptance evidence `native-four-window-activation.json`.

| Role | Android64 | Android32 | iOS64 | iOS32 |
| --- | --- | --- | --- | --- |
| BringToFront | `0xAA5388` | `0x7BA930` | `0x100E45AFC` | `0xC532C8` |
| SetVisible | `0xAA55BC` | `0x7BAAFC` | `0x100E45CB4` | `0xC534AA` |
| GetWindowActive | `0xAA5B14` | `0x7BAF44` | `0x100E46124` | `0xC538E0` |
| current window global | `0x1B4AE58` | `0x119393C` | `0x1022C45E0` | `0x1FCFB0C` |
| BaseWindow OnActivate | `0x8787FC` | `0x674AA0` | not identified | not identified |
| FireOnActivate | `0x876794` | `0x6731F0` | not identified | not identified |
| ActivateEvent Deliver | `0x883144` | `0x67C95C` | not identified | not identified |

The joint four-target pseudocode is:

```text
BringToFront(this):
    if current != this:
        if current:
            current.setPosition(current.getViewSize().width, 0)
            current.nativeInstance.OnReleaseCapture()
        current = this

SetVisible(this, visible):
    requestedVisible = visible
    this.setVisible(visible)
    if visible: this.BringToFront()
    else if current == this: current = prev ? prev : next

GetWindowActive(this):
    return current == this
```

None of those four BringToFront or SetVisible bodies posts a window activation
event. Android has stack cookies; ABI-specific offsets, Cocos virtual slots and
pointer widths differ. The window selection and boolean result agree.

The additionally retained Android event implementations agree:

```text
FireOnActivate(window, active):
    event = new ActivateEvent(window, active)
    PostInputEvent(event, REMOVE_POST)

ActivateEvent.Deliver():
    source.OnActivate(active)

BaseWindow.OnActivate(window, active):
    if !window.CanDeliverEvents(): return
    if window.GetWindowActive() != active: return
    if window.owner:
        PostEvent(owner, owner, active ? "onActivate" : "onDeactivate",
                  IMMEDIATE, no arguments)
```

FireOnActivate has zero IDA xrefs on both Android images, including the Thumb
entry-pointer variant. The BaseWindow handler's only identified caller is its
event Deliver thunk. The Android64 event has 24 bytes; Android32 has 16. Their
Owner offsets are 56/28 and their GetWindowActive virtual offsets are 48/24.
These are layout differences rather than different dispatch policy.

Exact UTF-16LE searches and complete xrefs on both iOS images identify the
Window method registration/default action callbacks and the separate System
event chain. They do not identify the BaseWindow/ActivateEvent route found on
Android. Link dead-stripping is a possible explanation, not established native
behavior; no missing iOS body is reconstructed from the local source. The
four-target window-selection evidence is sufficient to distinguish that
selection from a browser focus/event-delivery extension.

## Local comparison and Web implementation plan

Local MainScene keeps the same current-window selection and GetWindowActive
comparison. The existing Web BringToFront position restoration is a separate
presentation extension and must remain intact. Local BaseWindow::OnActivate
also rechecks GetWindowActive before dispatch, matching the retained Android
implementations. Simply queuing `false` on browser blur while GetWindowActive
still returns true would silently discard the deactivation event.

The Web boundary therefore observes actual browser focus and visibility
on the engine thread. GetWindowActive additionally requires that browser focus.
Each live window compares its last queued activation state against current
window selection, browser focus, visibility and the existing CanDeliverEvents
gate. Transitions enqueue the existing FireOnActivate event; no TJS action or
private script variable is invoked directly. Initial activation waits until a
visible enabled window can receive the event.

The engine's live Window list is enumerated each frame to access its current
Form. Transition state is stored on that Form, without caching window/native pointers
across frames or retaining an owner cycle. Posting queues the event without
running a script during enumeration; ordinary invalidation removes the window
from the list and clears pending input events. No extra event is sent for an
unchanged focused window. Browser blur/hidden, current-window switches and
return to a retained visible window all use the same transition rule.

Deactivation is queued before activation independently of window-list order.
The browser is queried once per frame through MAIN_THREAD_EM_ASM_INT, then the
engine thread performs the observation and event queuing before Application::Run.
Only MainScene changes; native branches and BaseWindow event dispatch remain
unchanged. A newly allocated Form initializes its own state to false, including
when an allocator reuses an old Form's address.

Implementation is complete; browser green acceptance awaits the parent's next
coordinated candidate. The existing red evidence demonstrates
that an attached script controller is never polled before this missing standard
activation event; device enumeration alone is not an input acceptance pass.
