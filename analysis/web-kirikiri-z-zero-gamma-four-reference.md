# Web Kirikiri Z zero-gamma boundary

## Four-reference evidence before implementation

All four binaries and matching readable IDBs were checked, opened through native
IDA MCP and freshly decompiled before the C++ change on 2026-11-14.

| Binary | LUT initializer | Status |
| --- | --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | TVPInitGammaAdjustTempData_c@0x89b704 | Complete fresh decompile |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | TVPInitGammaAdjustTempData_c@0x68b2f8 | Complete fresh decompile |
| Kirikiroid2_1.3.9_iOS_arm64 | sub_1000165A0@0x1000165a0 | Complete fresh decompile |
| Kirikiroid2_1.3.9_iOS_armv7 | sub_170B4@0x170b4 | Complete fresh decompile |

Shared source-level pseudocode, with three independent channel tables:

```text
amplitude[channel] = double(ceiling[channel] - floor[channel])
reciprocal[channel] = gamma[channel] == 0 ? DBL_MAX : 1.0/gamma[channel]
for i in 0..255:
    rate = log(double(i)/255.0)
    for channel in R,G,B:
        n = int(exp(rate * reciprocal[channel]) * amplitude[channel]
                + 0.5 + double(floor[channel]))
        table[channel][i] = clamp(n, 0, 255)
```

There is no reference default that changes zero gamma into one. Both signs of
zero enter the finite-DBL_MAX branch. In particular, with floor=0 and ceiling=255,
input 255 remains 255. Ordinary finite nonzero gamma follows the same formula.

Android64 selects the byte clamp with conditional instructions. Android32
increments an integer index and a double index in parallel. iOS64 uses a
conditional select for two reciprocals and reloads the red floor per iteration.
iOS32 spills temporaries around library calls. The <=0 versus <0 byte-clamp
forms have identical output at zero. These are compiler/ABI differences; none
explains away the shared finite reciprocal or endpoint behavior.

The existing `TVPInitGammaAdjustTempData_c` matches that pseudocode line by line:
three amplitude differences, three zero guards, one shared logarithm per index,
three exponentials, three round/truncate/clamp/store sequences. Preserve those
steps and the native builds' behavior.

## Explicit Z/Windows version difference

Supplemental evidence is the existing Kirikiri Z 1.4.0.8 Windows executable
`rpsisters.exe`, not an alleged upstream source repository. Native IDA freshly
decompiled its `sub_5BCB30@0x5bcb30` and disassembled all 116 instructions.
The Layer wrapper is `sub_592FE0@0x592fe0`, the bitmap method is
`sub_57B900@0x57b900`, and `sub_5C0A10@0x5c0a10` installs this LUT initializer.
Those wrapper/bitmap/function-table bodies were also decompiled in this
conversation. Improved SSE math-call prototypes now expose the reciprocal,
logarithm and exponential data flow in the LUT pseudocode.

This version unconditionally divides 1.0 by each gamma. At positive zero, all
indices below 255 use exp(-Inf)=0 and retain the rounded/clamped floor. Index
255 computes exp(log(1)*Inf)=NaN. CVTTSD2SI yields INT_MIN and the subsequent
negative clamp writes zero. At negative zero, the interior values overflow or
become NaN, and that same conversion/clamp writes zero throughout the channel.
These results come from the Windows instructions; no successful Wine visual
oracle is claimed.

Web already has an explicit, immutable-per-session Kirikiri Z mode, selected
before script initialization. This is a version compatibility boundary: apply
the Windows zero-gamma table behavior only in that mode. Do not change the
four-reference mobile algorithm or select behavior by game identity. Implement
the exceptional table values explicitly instead of introducing NaN-to-integer
C++ conversions, whose behavior is undefined and differs across targets.

For positive zero, retain the existing interior entries and replace only entry
255 with zero. For negative zero, clear that channel table. Leave every nonzero
channel unchanged. The normal and additive-alpha software render methods share
this LUT initializer, as do Layer and ImageFunction bitmap callers.

## Color and coordinate chain

The observed source is an existing TLG Layer, not PSB/MotionPlayer. Its script
loads a temporary Layer, calls adjustGamma, then blur, affineCopy to another
Layer, and an alpha-mask operation. The target Layer is parented to the ordinary
primary Layer tree. The defect is already present in the temporary pixel buffer,
before affine coordinates or Cocos presentation are applied.

The local chain traced before editing is:

```text
Layer.adjustGamma argument defaults and per-channel conversion (LayerIntf.cpp)
  -> BaseLayer::AdjustGamma, ClipRect and normal/additive-alpha selection
  -> iTVPBaseBitmap::AdjustGamma / AdjustGammaForAdditiveAlpha
  -> named AdjustGamma / AdjustGamma_a render method, gammaAdjustData parameter
  -> tTVPRenderMethod_AdjustGamma::SetParameterPtr -> shared LUT initializer
  -> DoRender: target rectangle, row pitch, TVPAdjustGamma / TVPAdjustGamma_a
  -> script affineCopy and alpha masking, target/owner/primary Layer composition
  -> LayerManager::DrawCompleted -> DrawBuffer::Blt
  -> BasicDrawDevice or PassThroughDrawDevice::Show
  -> TVPWindowLayer::UpdateDrawBuffer -> GetAdapterTexture -> Cocos DrawSprite
  -> viewport/design-resolution transform -> browser canvas/screen
```

Native software gamma parameter setters were freshly checked at
Kirikiroid2_1.3.9_iOS_arm64!sub_10032F764@0x10032f764 and
Kirikiroid2_1.3.9_iOS_armv7!sub_33405A@0x33405a: each passes its existing
embedded table and the caller's data to the installed initializer. Their
parameter-name methods return 0 for gammaAdjustData and -1 otherwise.
The Android64 normal pixel loop and the software function-pointer initialization
were also examined in this conversation. The final texture/Cocos handoff was freshly
decompiled at Kirikiroid2_1.3.9_Android_arm64-v8a.so!
TVPWindowLayer::UpdateDrawBuffer@0xaa5954, retaining texture scaling, texture
rectangle, disabled final blending and ResetDrawSprite. Existing four-reference
render-platform evidence is recorded in
`motionplayer_web_cocos_render_platform_boundaries_four_binary_2026-08-28.md`;
its Motion-specific portions are not attributed to this TLG scene.

The change touches only table values. No Layer, clipping, transform, alpha,
draw-device, texture ownership or Cocos geometry is altered. The public Web
renderer defaults to software. The separate OpenGL gamma shader is not changed
by this software-LUT fix and is not claimed as runtime-validated here.

## Validation scope

Before editing, the actual complete game's opening was replayed through the
normal PFB play API and UI. The existing screenshot checker failed on the
reported forehead region with 7,532 saturated red pixels. This is an existing
asset/scenario regression, not a fabricated image or generated fixture.

Post-change build, mode-boundary checks, actual Layer/shadow-buffer checks and
the same scene replay are recorded in ignored local acceptance evidence.
Unused gamma/renderer combinations without existing content are not represented
as having a native visual oracle.
