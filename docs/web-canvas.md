# Web Z drawing API

The browser build provides `Texture`, `Offscreen`, `Matrix32`, `ShaderProgram`
and `Canvas`, plus `Window.canvas`, `Window.drawCycle` and `Window.fireOnDraw()`.
These are additional Z APIs. Their registration does not replace the existing
Layer renderer. A Window activates Canvas rendering when its canvas is requested
or a positive draw cycle is set.

## Implemented interfaces

| Object | Supported operations |
| --- | --- |
| `Texture` | Construct from storage filename, Bitmap, or numeric dimensions; `copyRect`, `setDrawSize`; logical/memory dimensions, grayscale/POT information, real `nativeHandle`; nearest/linear filtering and repeat/clamp/mirror wrapping |
| `Offscreen` | RGBA texture and framebuffer construction; dimensions and real texture `nativeHandle`; Bitmap `copyRect`/`copyTo`; `exchangeTexture` with an equally sized RGBA Texture |
| `Matrix32` | Six float components; set/reset, translate/scale/rotate with optional pivot, preScale, multiply/premultiply, transform, Array view |
| `ShaderProgram` | Storage or direct GLSL source, compile/link errors, reflected uniform assignments, scalar/vector/matrix uniforms and up to three texture samplers |
| `Canvas` | Clear, four-color fill, one/two/three-texture drawing, atlas rectangles, capture, flush; render target, affine matrix, shaders, blend mode, clipping and culling; save/restore |
| `Window` | Lazy canvas, positive draw cycle, coalesced `fireOnDraw`, `onDraw` callback and presentation through the normal Layer update path |

Texture filtering/wrapping constants use the native GL values. Texture width and
height are draw dimensions; memory dimensions describe the allocation.
`nativeHandle` is an Emscripten texture GLuint, never a framebuffer identifier
or a fabricated pointer. `Offscreen.exchangeTexture` swaps actual texture
objects and reattaches framebuffers.

`Canvas.renderTarget` accepts an Offscreen or null. Null selects the window
surface. Target changes during `onDraw` bind the real framebuffer and viewport,
so external renderers can draw into the current target. Sampling that same
target as an input texture is rejected.

Save/restore covers the six matrix values, clipping rectangle, clipping enable
and culling enable. It does not restore blend mode, shader, target or clear
color. At each frame the window surface is cleared before a retained Offscreen
target is rebound. Positive drawCycle values start/change the requested rate;
zero and negative values leave an existing cycle unchanged. Browser event/frame
delivery limits the effective rate. Frame hooks and queued frames are removed
when the Window is invalidated.

## Pixel and GL boundaries

The Web Cocos port requests GLES 3, which Emscripten maps to WebGL2. The
Emscripten maximum-version linker setting alone does not select that context.
The port retains its opaque browser canvas and existing framebuffer bit counts.

Engine bitmaps contain top-to-bottom BGRA pixels. Texture upload converts them
to RGBA and reverses rows for bottom-left texture coordinates. Canvas quad UVs
map the top edge to v=1. Grayscale textures preserve the native alpha-channel
sampling convention. Framebuffer reads convert RGBA back to engine BGRA and
restore the bitmap's row direction; capture can request the opposite direction.

Canvas frames save and restore framebuffer bindings, viewport/scissor, program,
VAO/array buffer, texture units 0–2, blending, culling, depth/stencil enables,
color mask/clear color and pack/unpack alignment. Resource operations preserve
the caller state too. The Cocos GL state cache is invalidated afterward.
External renderers must restore every GL state they change and must not delete
borrowed Texture handles. They share the current framebuffer with Canvas.

The window surface is a GPU framebuffer. At frame end it is read back once and
published to an internal primary Layer through `AssignTexture`. This preserves
the existing software composition, window sizing and draw-device path. This
readback has a performance cost; it is not a zero-copy presentation backend.

## Explicit limits

`Canvas.drawMesh`, `draw9Patch` and `drawText`, nine-patch Texture decoding,
Font-derived Texture construction and Matrix32 skew operations currently throw
an explicit unsupported-operation error. Matrix44 and vertex/index buffer
classes are not supplied. Shader vertex attributes for generalized meshes are
not implemented. This API set therefore does not imply complete Z compatibility.

GLSL ES 1.00 identifier syntax is translated to ES 3.00 at the Web boundary;
compilation/link errors are reported with the real driver log. Unsupported
uniform types are errors. No storage name, game identity, script replacement or
empty drawing fallback selects behavior.

Game content, oracle captures and temporary probe scripts stay outside the
repository. Gameplay, input and save acceptance remain separate from graphics
API checks.

## Web Z Pointer events

The Web build emits `onPointerDown`, `onPointerMove` and `onPointerUp` only when
the host explicitly selects KiriKiri Z mode before initializing the script
engine. Each callback receives seven arguments:
`(kind, x, y, cx, cy, shift, id)`. Kind, shift and id are integers; the four
coordinates/contact dimensions are reals.

| Source event | Kind | Contact dimensions | Shift / id |
| --- | --- | --- | --- |
| Mouse down/up | Mouse button + 1 (left 1, right 2, middle 3) | 1.0 / 1.0 | Original flags / 0 |
| Mouse move | 6 | 1.0 / 1.0 | Original flags / 0 |
| Touch down/move/up | 7 | Original cx / cy | 0 / original touch id |

The original Mouse/Touch owner event and its DrawDevice/Layer delivery complete
before the Pointer event. Existing browser coordinate conversion and touch
gestures remain in effect; a gesture translated to a mouse event uses the mouse
mapping. The bridge rechecks window validity and event eligibility after the old
callback, so closing the window there suppresses the second event. A temporary
dispatch reference spans both calls.

Pointer callbacks use the normal immediate event dispatcher. Missing handlers
are ignored through its existing failed-call return semantics; thrown script
exceptions retain normal error handling. Standard engine mode emits only the
existing Mouse/Touch events. No default Pointer handler or game-specific input
adapter is installed.

At the Web SDL boundary, left/right/middle buttons are mapped explicitly to
Cocos button values; SDL and Cocos order right and middle differently. Side
buttons retain the existing numeric mapping.
