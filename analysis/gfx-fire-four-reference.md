# gfxEffect.dll / gfxFire — four-reference reconstruction

All addresses in each column belong only to the named binary below. This pass
checked the four inputs and matching IDBs, opened them with native idalib, and
freshly decompiled the complete component (67/78/84/84 functions), including
registration, constructor, helpers, properties, array ownership and rendering.

| Role | Kirikiroid2_1.3.9_Android_arm64-v8a.so | Kirikiroid2_1.3.9_Android_armabi-v7a.so | Kirikiroid2_1.3.9_iOS_arm64 | Kirikiroid2_1.3.9_iOS_armv7 |
| --- | --- | --- | --- | --- |
| Module/class initializer | 0x42d818 | 0x2ffce0 | 0x100322a0c | 0x327a1c |
| NCB class registration | 0x5c1008 | 0x4f62d4 | 0x100320a4c | 0x325598 |
| NCB member registration | 0x5bc2cc | 0x4f393c | 0x10031d9e8 | 0x3228d8 |
| Native constructor | 0x5c19d0 | 0x4f68e0 | 0x100321344 | 0x325f3c |
| init raw callback | 0x5bd07c | 0x4f3df0 | 0x10031df38 | 0x322daa |
| Layer binding | 0x5be4ac | 0x4f46d8 | 0x10031ea80 | 0x323744 |
| Allocate image/grid state | 0x5be860 | 0x4f4868 | 0x10031ec80 | 0x323984 |
| updateWarpMap | 0x5bd64c | 0x4f3f08 | 0x10031e128 | 0x322eb6 |
| updateCoolMap | 0x5bd8a4 (shared function chunk) | 0x4f411c | 0x10031e354 | 0x3230d6 |
| Copy seed alpha | 0x5bf320 (shared chunk; callback 0x5bd2bc) | 0x4f515c (shared chunk; callback 0x4f3e6c) | 0x10031f7dc | 0x324374 |
| cycle | 0x5be25c | 0x4f44f0 | 0x10031e7b0 | 0x323534 |
| Add seeds | 0x5bf530 | 0x4f5214 | 0x10031f8d4 | 0x3244a0 |
| Spring grid step | 0x5bf75c | 0x4f5310 | 0x10031fa08 | 0x32457c |
| Build scanline intersections | 0x5bfb98 | 0x4f558c | 0x10031fc60 | 0x3247f4 |
| Filtered sampling | 0x5c0144 | 0x4f5854 | 0x10031ff68 | 0x324ac6 |
| Nearest sampling | 0x5c0430 | 0x4f59a0 | 0x1003200fc | 0x324bec |
| Cool / blur / palette | 0x5c067c | 0x4f5b0c | 0x1003202ac | 0x324d38 |
| Seed rectangle clipping | 0x5c0a64 | 0x4f5cb2 | 0x1003204b4 | 0x324ef2 |
| Insert internal grid edge | 0x5c0c44 | 0x4f5e8c | 0x100320670 | 0x3250cc |
| Initialize boundary spans | 0x5bf0d8 (shared chunk) | 0x4f4e30 / 0x4f4f68 | 0x10031f5e0 / 0x10031f6fc | 0x32411c / 0x324258 |
| Shared MT unit draw | 0x9efd00 | 0x7506b0 | 0x1002c2368 | 0x2c7780 |
| Layer property cache | 0x56ac8c | 0x4bd9a0 | 0x1001feb40 | 0x1fd058 |

The UTF-16 module/class names were read as bytes in all four files:
`gfxEffect.dll` / `gfxFire`. All registered member-name literals were also
read and repaired with unsigned-short array types before fresh decompilation.
Methods are init, clearFireSeed, setFireSeed, setFireSeedPos, updateWarpMap,
updateCoolMap, applyRed, applyBlue, applyWhite, applyCustom,
setCustomColorTable, isRed, isBlue, isWhite, cycle. Properties are seedLayer,
targetLayer, seedX, seedY, numOfSeeds, randomSeed, forceH, forceV,
boundRangeH, boundRangeV, scalingCoeff, numOfBlurForCoolMap, textureFilterType,
coolRange, coolStrength, coolParticleDensityDenominator,
coolParticleDensityNumerator, edgeSmoothing. seedLayer/targetLayer are write-only,
numOfSeeds is read-only, and the three isColor members are methods.

## Shared source-level flow

```text
constructor:
  construct target and seed layer views; construct owned/borrowed 2D arrays;
  construct vector<Seed*> and four float grids;
  force=(-1,0), bounds=(8,4), blur=20, coolRange=1, coolStrength=160,
  density denominator=320, numerator=35, randomSeed=0,
  filter=nearest, scalingCoeff=float bits 939239554, edgeSmoothing=false;
  palette=shared red table; rng=shared clock-seeded native MT owner.

init(layer, count=1):
  require first argument; bind layer through cached dispatch properties;
  resize the pointer vector, allocate each Seed's empty byte array;
  borrow target pixels with signed pitch;
  clear two owned byte maps; allocate/clear four float grids (width/16+1,height/16+1);
  updateWarpMap(); rebuild boundary span storage; updateCoolMap().

setFireSeed(layer,index=0): bind seed layer, if index is in the vector,
  allocate/clear that seed byte array and copy every source pixel's alpha.
setFireSeedPos(x,y,index=0): require x/y, store only for an in-range index.
clearFireSeed(index=0): destroy only the selected seed array if in range.

updateWarpMap(): fix outer grid coordinates/velocities; inner coordinates use
  16 - force - bounds/2 + randomUnit()*bounds, with zero initial velocities.
updateCoolMap(): clear map; choose density*width*height/denominator particles;
  each stores byte(randomUnit()*coolRange + coolStrength); repeat native rounded
  separable [1,2,1]/4 blur with its original row traversal and boundary handling.

cycle(): saturating seed addition after rectangle clipping; accumulate spring
  forces from the 5x5 grid neighborhood and then integrate interior vertices;
  collect raster edge intersections in eight-scanline packed blocks;
  insertion-sort each scanline; interpolate signed 16.16 texture coordinates;
  sample nearest or the native four-sample fixed-point filter;
  subtract cooling with saturation; horizontal then vertical rounded blur;
  map bytes through palette, clear the last two rows, call target.update with
  its imageLeft/imageTop/imageWidth/imageHeight values.
```

The existing local `NI_LayerExBase` matches the four native base helpers and is
reused as GfxLayer's base, including its seven cached properties and exit hook.
Layer caches use `TJS_IGNOREPROP`, retain the obtained dispatch properties, and
read imageWidth/imageHeight/mainImageBufferForWrite/mainImageBufferPitch.
The derived view also caches left/top and reads imageLeft/imageTop. The borrowed
target/seed dispatch pointers are not replaced by owning smart pointers.

Array objects retain width, height, data, row pointers and an ownership bit;
borrowed signed pitches are preserved. Owned resize clears even when reusing
an allocation of the same dimensions. The seed container is a pointer vector,
not a value vector or a shared ownership graph. The new code follows these
construction and destruction boundaries rather than selecting ABI byte offsets.

The force setters accept [-7,7], bound setters [-7,8], scalingCoeff only
(0,0.01), blur/density setters positive values, cooling setters [0,255].
textureFilterType and edgeSmoothing store booleans. randomSeed only stores its
integer field; it does not reseed the shared generator. The existing native MT
owner in EmoteBlinkRng is shared; gfxFire draws one tempered word / 2^32, unlike
the blink canonical-real helper which consumes two words.

Nearest sampling traverses the scanline-count vector, while filtered sampling
traverses the work-image height. The vector covers the grid's complete 16-pixel
rows. This caller dimension boundary is retained; the real-asset rendering
probe uses a target rounded to complete grid cells. The clock-seeded shared MT
does not supply a deterministic four-platform pixel oracle.

Preserved source quirks jointly present in all references: left clipping tests
nonnegative destination-left; the right boundary's increment assigns both
texture coordinates from the same updated temporary; randomSeed and
edgeSmoothing are stored but do not add extra computations to cycle. Cooling's
last boundary write targets row 1, and the texture span's U step subtracts
destination-left from the next U coordinate. These are
not silently replaced by a different fire algorithm.

## Constants and differences

Three 256-word palette tables were read in every file and compared byte-for-byte;
all four copies agree. Red/blue/white bases respectively:

- Android arm64: 0x14c8104 / 0x14c8504 / 0x14c8904.
- Android armv7: 0xd7c0d4 / 0xd7c4d4 / 0xd7c8d4.
- iOS arm64: 0x1015d9bf0 / 0x1015d9ff0 / 0x1015da3f0.
- iOS armv7: 0x1469230 / 0x1469630 / 0x1469a30.

Android arm64 inlines parameter conversions, several array helpers, cooling,
sampling and rectangle operations, with NEON vectorized rounded blur. Other
files expose more small helper calls. iOS armv7 has SJLJ exception cleanup and
NEON scalar-float lowering; iOS arm64 uses standard unwind paths. Pointer and
STL layout widths differ. These do not change the shared named members,
defaults, loops or order. The Android arm64 cool callback is a prologue at
0x5bd8a4 attached by IDA as a chunk of 0x5be860; raw bytes, disassembly and the
NCB callback pointer establish this mapping; define_func reports that it is
already owned. No single-file offset is used as a portable class layout.

## Local implementation mapping

GfxFire class construction, raw callbacks and NCB registration follow the
corresponding rows above. GfxLayer performs the native cached dispatch access;
GfxArray implements owned/borrowed row storage; Seed extends that array with
position; the pointer vector, four float grids and packed scanline storage
remain separate. GfxFire's updateWarpMap/updateCoolMap/cycle helpers preserve
the pseudocode's intermediate steps. Palette constants are shared arrays;
the extra unit-draw entry reuses the existing shared native MT state.

The Web build registers this module through the normal NCB registry for every
caller of gfxEffect.dll. There is no game identity, script patch, dummy class
or exception suppression. Runtime validation uses existing private game assets
outside Git; successful startup alone is not evidence of a rendered fire cycle.
