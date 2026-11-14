# layerExAlpha MaskRect: four-reference restoration

## Native mapping established before the source change

All four binaries and their paired IDBs were checked and opened in native IDA
MCP. The complete registration and MaskRect bodies were freshly decompiled.
UTF-16 literals initially shown as one character were read as bytes, repaired
as unsigned-short arrays and re-decompiled.

| Binary | Module initializer | Layer registration | MaskRect |
| --- | --- | --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0x42da98 | 0x5d43b0 | 0x5d621c |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0x2fff9c | 0x5033d8 | 0x5044c0 |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x1000464c4 | 0x10003b8bc | 0x10003ce94 |
| Kirikiroid2_1.3.9_iOS_armv7 | 0x44850 | 0x390a8 | 0x3a280 |

Each module initializer names layerExAlpha.dll, the attached class
layerExAlpha, and the target Layer. The shared registration binds the exact
MaskRect member to the listed body (not ExMaskRect, ExMaskRect2 or
LevelMaskRect, which also contain its name). The iOS64 eight-argument NCB
invoker is 0x10004450c, conversion/call body 0x10004464c, instance getter
0x100042e44, base constructor 0x100042f24 and reset 0x100042fa8. It rejects
fewer than eight arguments, obtains or creates the attached native instance
and resets its dimensions, writable buffer/pitch (software only), and clip
rectangle before calling the member. The base fields and redraw virtual
match the existing layerExBase_GL abstraction.

## Common pseudocode

```text
MaskRect(dx, dy, sourceObject, sx, sy, width, height, type):
    get native Layer instance of sourceObject; throw "Not Layer" on failure
    if type == 0:
        destination.GetMainImage().CopyRect(
            dx, dy, source.GetMainImage(), sourceRect, alpha-plane)
    else if software renderer:
        sourcePitch = source.GetMainImagePixelBufferPitch()
        sourceBytes = source.GetMainImagePixelBufferForWrite()
        sourceAlpha = sourceBytes + sourcePitch*sy + sx*4 + 3
        destinationAlpha = writableBuffer + pitch*dy + dx*4 + 3
        switch type:
            1: per pixel min(dstAlpha + srcAlpha, 255)
            2: per pixel max(dstAlpha + srcAlpha - 255, 0)
            3: per pixel (dstAlpha * srcAlpha) >> 8
            default: TVPAddLog("layerExAlpha: noting type")
        advance each row using each image's pitch
    else:
        switch type:
            1: CompileRenderMethod ExAlphaAdd(texture0), blend ADD,0,1,1,1
            2: CompileRenderMethod ExAlphaSub(1-texture0.a), REVERSE_SUBTRACT,0,1,1,1
            3: GetOrCompileRenderMethod AlphaMask(texture0), ADD,0,1,0,SRC_ALPHA
        source texture with rectangle (sx,sy,sx+width,sy+height)
        destination.GetMainImage().GetTextureForRender(true, null)
        OperateRect(method, destinationTexture, null, destinationRect, oneSource)
    redraw through the attached instance's virtual method
```

The software source accessor is deliberately the writable accessor, even
though this method only reads it: iOS64 0x10007a980 sets the image-modified
flag and calls the bitmap write accessor. Its use is shared with base reset.
Do not replace it with the read accessor merely because the pixels are inputs.
The diagnostic helper iOS64 0x1002591d4 delegates to 0x100258914, the timed log
queue/file/output path; this is a log, not an exception.

## Per-binary differences and boundaries

Android64 inlines several NCB allocations/registration branches; Android32 and
both iOS builds outline their corresponding method factories. Android64 and
iOS64 use eight-byte pointers, with destination native Layer at +8, writable
buffer +24 and pitch +32; the two 32-bit builds use +4, +16 and +20. These
are compiler field placements, not manually reproduced padding.

The 32-bit saturation case 2 can be expressed as the equivalent addition-plus-
one and carry/conditional sequence. The 64-bit functions subtract 255 and
clamp the signed intermediate. All four multiply two unsigned alpha bytes and
shift by eight, so software division by 255 would be a behavioral change.
The CPU loops have no extra rectangle clipping in this function. All have
a diagnostic-only software default branch. The GPU switch has no default
initialization of its render-method local; invalid nonzero types remain
outside defined rendering behavior and are not silently converted to type 0.

Each GPU branch initializes its method once; guard routines, exception cleanup,
register spills and iOS32 SjLj bookkeeping differ by compiler/ABI. The same
shaders and blend constants appear in all four. The normalized GPU multiply
and software 8-bit shift are preserved as their original respective paths.
Only the Web build's existing renderer boundary determines which path runs.

## Local correspondence and scope

Independent case-sensitive and insensitive source searches, the complete
plugin source list, NCB registration inspection, and an actual full-package
runtime failure confirm no local layerExAlpha module or Layer.MaskRect binding.
layerExBase.hpp already supplies the matching native-Layer acquisition,
software buffer reset, clip rectangle and redraw lifecycle. layerExAlpha.cpp
will derive from that base and use its NCB instance hook, resetting on each
call. The native method's CopyRect branch maps directly to the existing
bitmap alpha-plane operation. The three CPU cases preserve byte offsets,
saturation, shift and separate pitches. The GPU cases map directly to the
existing render manager and its shader/method caches, texture rectangle array,
GetTextureForRender and OperateRect; no replacement canvas renderer is needed.

Registration and CMake add the actual layerExAlpha.dll module. The complete
31-member restoration and its additional four-reference evidence are recorded
in layerexalpha-complete-four-reference.md; no dummy functions are advertised.
The supplied complete game and its existing transition provide real runtime
material. There is no manufactured image fixture or new synthetic pixel
oracle. Runtime acceptance must cross the actual mask transition and continue
ordinary input; title-only smoke testing cannot cover this call.

The helper initially described as GetOrCompileRenderMethod for modes 1/2 was
corrected before runtime acceptance: iOS64 0x100323b40 explicitly identifies
itself as CompileRenderMethod and asserts the method name is absent before
compilation/registration. Mode 3 uses the distinct hint-enabled lookup helper
0x100323bf4. Keep these different intermediate steps in the restored code.
