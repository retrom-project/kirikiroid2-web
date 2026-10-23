# Web Host bookmark completion

The downstream-only `HostBookmarkBridge.cpp` exposes `krkr2_host_*` for a Web
consumer. It is isolated from reconstructed native engine code, has no native
reference counterpart, and does not change KAG scripts or bookmark formats.
This repair follows the user's existing authorization to diagnose and fix
new-session checkpoint restore. It does not extend the separate, limited
exception for the native `windowEx.getDisplayMonitors` plugin method.

A retained real-game checkpoint demonstrated two successive defects: startup
first failed on the display enumeration stub; after that API was implemented,
the loader became ready and loaded the checkpoint, but the Host bridge rejected
the result. The loaded game's override of `loadBookMarkFromFile` calls the base
method and performs additional work without forwarding its return value.
Consequently the top-level `loadBookMark` returns void even after restoration.
The old bridge reduced all results to bool and reported rejection.

The Host now observes callable `restoreFlags` only during the requested original
`loadBookMark` invocation on that same KAG object. Both the direct path and the
framework's deferred `loadFunction` path use the same scoped observation. A
void loader result is accepted only if that invocation completed `restoreFlags`
successfully and the entire loader returned normally. Explicit falsy results,
dispatch failures, exceptions, and hook cleanup failures remain failures.
The restore observer does not itself publish success; post-restore script work
can still fail. A prior stable flag, unrelated slot, or elapsed time is not
completion evidence. Save/capture still requires its existing explicit result.

Temporary callbacks retain only the original function, not their KAG receiver;
the invocation already owns that receiver. Removal restores the original only
if the property still holds this callback, preserving script replacements.
Retained callback aliases remain callable but hold no pointer to stack-local
state and cannot complete a later Host operation. Host stop still drains JSPI
before canceling observers.

The existing boundary harness compiles the actual bridge and checks both
entrances, causal void completion, void without a witness, explicit rejection,
restore/outer exceptions and dispatch errors, replacements, retained aliases,
readiness/cancel, and reference balance. It supplies fake TJS/event dependencies
and does not replace actual consumer validation. Private original-checkpoint
replay and save/new-session/input evidence remain in the consumer PFB.
