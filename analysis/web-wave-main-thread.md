# Web wave playback scheduling

## Evidence and scope

This repair is based on the reported browser stack and the Web implementation.
The user explicitly waived the four-reference-binary/native IDA prerequisite
for this task. It does not claim to reconstruct native binary behavior.
Upstream was synchronized to `13dda190f8370d02b6cf59286a088529b355658c` first.

The supplied frozen-renderer stack was mapped against the exact previously
installed Wasm, not a newly linked build. It reaches the main-browser-thread
futex spin through `TVPRunWaveSoundMainContinuation`, the asynchronous play
continuation, `FillBuffer`, and label rescheduling. The main thread holds the
buffer's `BufferCS` and is waiting for `TVPWaveSoundBufferVectorCS`.

The playback thread takes these locks in the opposite order: its `Execute`
holds `TVPWaveSoundBufferVectorCS` while calling each buffer's `FillBuffer`,
which takes `BufferCS`. The owner's stack was not supplied, so the full ABBA
cycle is an identified source-level possibility, not an observed second stack.
This lock order remains in the synchronized upstream revision.

There is another reason not to retain a playback worker around OpenAL calls:
Emscripten proxies its browser audio operations to the main thread. A worker
holding an audio lock while waiting for that proxy cannot make progress if the
main thread is itself waiting for the lock. Moving only label rescheduling out
of one critical section does not establish a safe ownership model.

## Implemented Web boundary

Keep decoder work in the existing background continuation pipeline. Submit
decoded L1 audio buffers and schedule labels on the browser main thread. The
periodic callback must neither run game script directly nor synchronously read
content when the decoder has not produced a buffer. Label delivery remains an
engine event, and a buffer underrun yields to the existing decoder work.

The Web callback must be canceled, and queued label notifications removed,
when the last wave buffer destroys the playback scheduler. Native scheduling
must retain its existing thread and timing behavior.

## Validation

The SDK 6.0.9 Web Release build and candidate verification pass. Existing
Content I/O VLFS, ZIP-cache, pthread JSPI, main-loop scheduler, and paired Web
asset tests pass. There is no existing redistributable audio fixture that
reproduces this lock cycle; no synthetic game was added to the core repository.

In Retrom's isolated PFB, Brave 1.95.104 (Chromium 153.0.8010.53) on Linux
restored the operator's original bookmark, displayed both choices, accepted a
choice, and advanced the dialogue. The repaired playback scheduler then
accepted 400 rapid gamepad A cycles (400 matching mouse-down/up pairs), kept the
toolbar usable, and produced a new native bookmark checkpoint. A browser with
no prior user activation needs a trusted gesture after audio initialization;
otherwise this game's opening voice wait can outlast the host restore timeout.

A separately built control at synchronization commit `c437200c`, with the
original upstream playback worker and no temporary VM probe, also passed the
original bookmark's choice scene. Therefore the prior caught TJS exception is
resolved by the upstream update for this sample; it must not be attributed to
the local scheduler patch or presented as proof of the audio lock diagnosis.

The whole-renderer freeze was reported on Windows 10 with a physical gamepad.
It has not been reproduced locally on Linux. The source-level lock path is
removed and the reported input pattern is exercised, but this is not a claim
of Windows hardware validation. No temporary diagnostic logging ships.
