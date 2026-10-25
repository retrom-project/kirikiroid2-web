Web native exit is a browser lifetime boundary. A JSPI frame can retain TJS
objects while the Host is still reading content or exporting a native save.
Releasing the script engine inside that frame leaves the save observer with an
invalid receiver before the Host has received any exit notification.

The changes to shared engine entry points are guarded by `__EMSCRIPTEN__`.
Desktop/mobile `TVPTerminateSync` and the non-Web `Application::Run` shutdown
branch keep their existing calls and order. No claim about reconstruction of
the four Android/iOS reference binaries is made here: those binaries do not
have a browser Module, JavaScript Promise Integration or a Host checkpoint
boundary. This is the explicit platform boundary allowed by `CLAUDE.md`.

For Web, confirmed synchronous exit uses the existing asynchronous termination
intent and `EAbort` to finish the active frame. The Host receives
`onNativeExitRequested(status)` synchronously, closes checkpoint availability,
then waits for the frame before calling the JSPI export
`krkr2_host_finalize_native_exit`. That export flushes the registered native
system-data callback while available, detaches the observer, and invokes the
ordinary engine teardown. Only an already invalidated game receiver can be
skipped during confirmed exit; ordinary observer and flush failures remain
errors. Final capture packages the complete VLFS overlay after teardown.

The C++ boundary regression executes the actual `TVPTerminateSync` body under
both Web and native compilation. The real observer/flush coordinator tests
cover duplicate requests, a suspended frame, an invalidated game object, flush
failure and nonzero exit. The runtime regression exercises the native callback
through the public Provider lifecycle rather than manually invoking only the
Emscripten `onExit(0)` callback.
