# Cubism plugin in the browser

The Web build provides the `krlive2dcubism.dll` script module as native TJS
bindings backed by Cubism Core for Web and WebGL2. It implements the original
plugin's Cubism 3 contract. It does not load a Windows DLL in the browser.

Cubism Core is a separate, proprietary dependency. The repository does not
download it, include it, or accept its license on behalf of a builder. A build
that uses models must provide an appropriately authorized, unchanged Core
script before the engine starts, together with the notices required by that
SDK's redistribution list. Without Core, opening a model reports
`Live2D Cubism Core for Web is not installed`.

## Resource loading and script API

`Live2DCubism.open()` reads a model3 JSON document through normal engine
storage. MOC3, textures, physics and external parameter references use that
document's directory and the engine's normal storage search. It does not
rewrite resources, inject a game script, or select behavior by a title, asset
hash or filename.

`Live2DCubism` exposes model loading and closing, parameter names and ranges,
dynamic parameter properties through TJS `missing`, reset operations,
parameter multipliers, voice lip sync, physics controls, transforms, drawable
hit testing, and drawing into the current Canvas target. `textureFiles` lists
the model's texture references; scripts create ordinary `Texture` instances
and pass their `nativeHandle` values to `draw(handles, width, height)`.

The external physics extension uses the original `ExternalParamter` spelling
in `FileReferences`. It maintains an independent parameter pool with `Name`,
`Min`, `Max`, `Default`, `Output`, optional `Destination` and `EnableIndex`
fields. `loadExternalPhysics(parametersFile, physicsFile)` replaces that pool
and its rig. Enabled external outputs are added to model parameters and
clamped to their declared ranges after the standard rig runs.

`Live2DAnimation` loads Cubism motion3 JSON and supports linear, restricted
Bezier, stepped and inverse-stepped curves for parameters and part opacity.
Its elapsed time advances when `applyAnimation(animation, delta, weight)` is
called. TJS timeline dictionaries remain the responsibility of their script
animation system; they are not converted to a different motion format.

## Browser boundary

The C++ bridge copies MOC3 bytes into Core's own heap and uses opaque integer
handles. Core pointers are never reinterpreted as engine pointers. Actual
Core operations and WebGL calls run on the main thread; pthread callers use
Emscripten's main-thread bridge.

The renderer consumes Core's real vertices, indices, visibility, opacity,
render order and clipping masks. It uses normal, additive and multiply
blending and preserves the native projection and texture UV convention.
Engine textures use bottom-left pixel orientation. Draws share the current
Canvas framebuffer and restore every WebGL state they mutate, including
framebuffer, viewport, shader, VAO, buffer, texture units, blend and culling.

Model `close()` releases its Core and GPU resources. Static `uninitialize()`
releases browser GPU resources for all live models; a later draw recreates
them from the retained model data. Unlike the native plugin's process-wide
shader and mask cache, browser renderer resources belong to each model.

The Web build links the three `cpp/plugins/cubism` translation units into
the ncbind plugin archive. Its pre-JS inputs are `cubism-physics.js`,
`cubism-animation.js`, `cubism-controller.js`, then `cubism.js`. The bridge
declares its Emscripten JS library dependencies and requires exported
`_malloc` for returned UTF-8 strings. The helpers contain no network loader.

## Validation boundaries

Graphics checks must cover window and offscreen targets, clipping masks,
changed vertices and render order, texture orientation and caller GL state.
Use existing model data and compare the standard and external physics rigs
against the native plugin, retaining its float32 rounding. Motion checks need
actual motion3 documents covering their segment types.

Graphics and numeric checks do not establish complete browser gameplay or
saving. Core for Web initialization, support for the supplied MOC3 versions,
and the complete TJS/Canvas/Core game path require validation with an
authorized Core input. Temporary models, captures, measurements, native tools
and raw evidence stay in ignored local acceptance storage.
