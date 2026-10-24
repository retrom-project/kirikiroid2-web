# Web windowEx display enumeration boundary

This change has a user-approved exception (2026-10-24) to the four-reference
decompilation prerequisite, limited to Web `System.getDisplayMonitors` and its
verification. The four native inputs remain unavailable. This is a browser
adaptation to the public plugin interface, not native source reconstruction.
Other plugin methods and non-Web implementations are outside this exception.

## Contract and platform mapping

The plugin author's [public API documentation](https://github.com/wamsoft/windowEx/blob/518a5897a1803507de8a4cb92dc08f4f42561180/manual.tjs#L554)
defines an Array of display dictionaries, either all displays (no arguments)
or those intersecting `(x,y,w,h)`. Each dictionary has `name`, `primary`,
`monitor`, `work`, and `intersect`; rectangles have `x,y,w,h`.

An ordinary embedded browser canvas cannot enumerate or move native desktop
windows. This implementation exposes one virtual primary display named
`WebViewport`. It uses the existing `TVPGetScreenSize(0,...)` logical coordinate
convention (width 2048, height from the active view's aspect ratio). Window
origins are already virtual `(0,0)` in `iWindowLayer`; canvas backing pixels
include devicePixelRatio and must not be substituted for these coordinates.
The virtual work area equals the display area because it has no OS taskbar.
This does not enumerate physical monitors or request screen-management access.

The Web boundary accepts exactly 0 or 4 arguments. It returns a real empty TJS
Array for disjoint rectangles, edge-only contact, and nonpositive query area.
Coordinates must fit `tjs_int`; edge arithmetic is widened before addition.
Invalid counts/ranges and failed geometry/container operations are errors,
not successful `void` results. A call with no result has no side effects.
These error/argument details define this Web boundary; the public manual does
not establish the four native binaries' exact conversion or error behavior.

## Failure and verification

A real existing checkpoint supplies saved window coordinates during startup.
The game then enumerates intersecting displays and reads the Array's `count`.
The previous successful `void` stub caused a script exception before startup
completed, so the Host bookmark loader never became ready. Increasing the
restore timeout or deleting saved preferences would conceal the API defect.

`test-display-monitors.py` compiles the actual production boundary with minimal
NCB containers. It checks full result fields, intersection/clipping, empty
results, overflow boundaries, bad counts/ranges, and failure propagation.
It does not simulate the TJS interpreter or prove NCB reference management.
The retained original game/checkpoint replay in the consumer PFB is the actual
TJS/NCB integration check, including startup, bookmark load, visible resume,
continued input, and another save/new-session restore. Private game/save data
and screenshots stay in that PFB's ignored acceptance evidence.
