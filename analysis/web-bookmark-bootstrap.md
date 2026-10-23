# Web Host restore bootstrap

`HostBookmarkBridge.cpp` is a downstream Web consumer boundary without a native
reference counterpart. This change coordinates an explicit Host restore request;
it does not reconstruct or modify the native script engine, KAG, game scripts,
bookmark format, or plugins. It does not extend the separate user-approved
exception for `windowEx.getDisplayMonitors`.

Consumer testing exposed an early-load defect after display enumeration and
void-result completion were repaired. The KAG loader was callable while the
first scenario was still registering plugins/macros. Restoring immediately
replaced that scenario. The saved page rendered, but the next input stopped
with a missing macro. The game's save configuration intentionally omitted macro
definitions; neither a different serializer nor injected macro definitions would
address the initialization ordering. A 20-second delay also failed because the
startup scenario was blocked on a standard interactive `waitclick`.

The readiness query remains side-effect free and reports whether a request can
be queued between complete JSPI ticks. The accepted request remains pending
while the coordinator lets the first scenario reach a stable load boundary.
A running conductor always waits. A plain KAG entry before its first process
can load when no conductor is running; this preserves the existing entry case.

Before the first stable boundary, only an explicitly nonstable **main** conductor
at the standard `waitclick` tag may receive a bootstrap signal. Its process
timer must be stopped, timeout timer explicitly disabled, and wait dictionary
must contain exactly one callable `click`, without arguments or other signals.
The coordinator calls the callable `trigger("click")` with that conductor as
receiver and requires an explicit successful integer result. This consumes a
script click barrier during restore bootstrap; it is not a read-only readiness
test and does not claim that every custom use of `waitclick` is initialization.

There is at most one signal per engine tick and per dynamic conductor/storage/
line/call-depth point, recorded before calling script. A request acknowledges at
most 32 distinct points. Repeated or unknown waits remain pending under the
consumer's existing 60-second restore budget and cancellation. Ordinary fresh
launches do not enter this path. Text/page/choice stops, timed waits, extra
conductors, mixed signals, and incomplete or ill-typed state are not advanced.
No DOM input, fixed startup delay, title-menu selection, game name, resource
name, or coordinates determine production behavior.

Queued callbacks capture only a request identity. Active state owns its script
references; cancellation clears it before teardown. A callback validates the
identity before script access and after script execution. Replacing KAG during
the request rejects it. Exceptions or rejected signals cannot load a bookmark
or publish success. The actual loader and existing causal `restoreFlags`
completion contract still decide restore success after bootstrap.

The boundary harness compiles this implementation and checks running-to-stable
ordering, allowed signals, negative waits/types, repeat and signal budgets,
cancellation/getter access, KAG replacement, and reference balance alongside the
existing loader tests. The new ordering assertion fails against the preceding
bridge. Real consumer validation must separately prove visible text progression,
normal save, a different Launch restoring that save, and further progression;
neither a successful load state nor canvas input events alone prove playability.
Private evidence stays in the consumer PFB.
