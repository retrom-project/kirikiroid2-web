# Software UDFlip source lifetime

## Four-reference mapping

Fresh native IDA decompilations were collected before this change. Full output
is retained in the ignored PFB acceptance workspace as
`takeover-four-render-evidence.json`.

| Binary | Bitmap UDFlip | Software DirectCopy::DoRender | GetTextureForRender | Software OperateRect (one source) |
| --- | --- | --- | --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | 0x7FB180 | 0x851D68 | 0xA785B8 | 0x85CAF8 |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | 0x62B0CC | 0x65DE28 | 0x79A57C | 0x664CAC |
| Kirikiroid2_1.3.9_iOS_arm64 | 0x100408E0C | 0x10032DFFC | 0x100052738 | 0x100338FF8 |
| Kirikiroid2_1.3.9_iOS_armv7 | 0x3F09F0 | 0x332A44 | 0x519B8 | 0x33C308 |

All four also map the software multi-source dispatcher to, respectively,
0x84B8DC, 0x659BCC, 0x1003275A0 and 0x32C9F0. The observed shared flow is:

```text
UDFlip(rect):
    reject an out-of-bitmap rectangle
    source = current texture; sourceRect = rect with top/bottom exchanged
    method = render manager's Copy
    destination = GetTextureForRender(method.IsBlendTarget(), rect)
    OperateRect(method, destination, source, rect, [(source, sourceRect)])

GetTextureForRender(blend, rect):
    if blend or no rect or partial replacement: Independ()
    else: IndependNoCopy()
    return current texture

software DirectCopy(destination, source, sourceRect):
    pixelSize = destination format is Gray ? 1 : 4
    if sourceRect.top > sourceRect.bottom:
        for y in [0, destinationRect.height):
            memcpy(destination.writeRow(destinationRect.top+y)+left*pixelSize,
                   source.readRow(sourceRect.top-1-y)+left*pixelSize,
                   width*pixelSize)
    else: retain the existing horizontal/ordinary-copy branches
```

All four Copy `IsBlendTarget` implementations return true (respectively
0x84FB48, 0x65CAC8, 0x1002EE240 and 0x2EE896). Independence only separates
shared/static textures; it does not necessarily copy an already writable,
uniquely owned texture. Thus the reference software row loop itself does not
protect a self flip. This is not presented as a missing swap in the reference.

The iOS variants retain dimension assertions in DirectCopy; Android omits them.
Android arm64 inlines the independence branches; the other three call separate
helpers. Pointer widths, refcount offsets and libc memcpy spellings differ.
None of these differences introduces a source snapshot in the software loop.

## Runtime evidence and platform boundary

The actual Web confirmation produced 534x266 RGBA pixels with all 266 rows
identical to their opposite row before WebGL consumed them. Source/target
aliasing lets the first half overwrite the rows subsequently read by the second
half. The displayed dialog therefore contains both upright and inverted buttons.

Web uses the software renderer, including CPU-readable buffers passed to native
plugins. Its pixel copy cannot rely on GPU source/target separation. The local
OpenGL manager explicitly snapshots a source that aliases its render target,
but the shared software reference path above has no such step. Retain that
reference path and document one deliberate renderer-boundary correction: when
the vertical-copy source aliases the actual destination, own a separate source
texture for the duration of DoRender. No change is made to TJS flipUD, bitmap
independence, coordinate transforms, plugin row orientation or game scripts.

The local implementation copies through the existing software texture copy
constructor, redirects only the borrowed source argument, runs the original
Gray/RGBA copy loops unchanged, then releases the owned texture on return.
Nonaliasing copies and ordinary overlapping blits keep their existing paths.
The regression runs the production DirectCopy class with pixels decoded from
the repository's existing PWA icon, not a new game or generated image fixture.
