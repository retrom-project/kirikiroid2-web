# layerExAlpha complete module: four-reference evidence

All 31 registered methods were freshly decompiled in all four checked native
IDBs through native IDA MCP before extending the local implementation. The
registration mapping is as follows. No MaskImageRideRect6 is registered in any
of the four modules; that number is not invented as an alias.

| Member | Android arm64-v8a | Android armabi-v7a | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| turnAlpha | 0x5d4c04 | 0x503830 | 0x10003bdac | 0x39510 |
| turnAlpha2 | 0x5d4dc4 | 0x503994 | 0x10003bf48 | 0x396f8 |
| turnAlpha3 | 0x5d4f80 | 0x503aec | 0x10003c0e0 | 0x398dc |
| turnOutline | 0x5d502c | 0x503b50 | 0x10003c188 | 0x3993e |
| AlphaColorRect | 0x5d5204 | 0x503bb8 | 0x10003c22c | 0x399a4 |
| AlphaDivide | 0x5d5400 | 0x503c42 | 0x10003c2f8 | 0x39a2c |
| AlphaAdd | 0x5d54a8 | 0x503caa | 0x10003c39c | 0x39a92 |
| AlphaAdd2 | 0x5d5708 | 0x503d6c | 0x10003c4c4 | 0x39b46 |
| BlackForAlpha | 0x5d57cc | 0x503df0 | 0x10003c584 | 0x39bc0 |
| AlphaBlend | 0x5d5894 | 0x503e84 | 0x10003c648 | 0x39c54 |
| ColorAlphaAdd | 0x5d5950 | 0x503f14 | 0x10003c700 | 0x39cd0 |
| ColorVectorEffectAdd | 0x5d5b8c | 0x503fd4 | 0x10003c80c | 0x39d84 |
| ExtraColorVectorEffectAdd | 0x5d5cdc | 0x504114 | 0x10003c9a8 | 0x39eb4 |
| WhiteToBlueBlurAdd | 0x5d5f98 | 0x504390 | 0x10003cd2c | 0x3a158 |
| MaskRect | 0x5d621c | 0x5044c0 | 0x10003ce94 | 0x3a280 |
| ExMaskRect | 0x5d6784 | 0x504920 | 0x10003d3dc | 0x3a7fc |
| ExMaskRect2 | 0x5d6ea0 | 0x504ccc | 0x10003d958 | 0x3ac4c |
| LightForAlpha | 0x5d764c | 0x505028 | 0x10003de54 | 0x3b040 |
| LuminanceForAlpha | 0x5d776c | 0x5050e8 | 0x10003df48 | 0x3b0e0 |
| LevelMaskRect | 0x5d78d0 | 0x505204 | 0x10003e080 | 0x3b1dc |
| LevelMaskRect2 | 0x5d7ba8 | 0x505350 | 0x10003e27c | 0x3b3cc |
| MaskImageRideRect | 0x5d7fe8 | 0x5054cc | 0x10003e4cc | 0x3b600 |
| MaskImageRideRect2 | 0x5d867c | 0x505a14 | 0x10003eaac | 0x3bcdc |
| MaskImageRideRect3 | 0x5d8b10 | 0x505dbc | 0x10003efcc | 0x3c22c |
| MaskImageRideRect4 | 0x5d8f94 | 0x506154 | 0x10003f4d4 | 0x3c76c |
| MaskImageRideRect5 | 0x5d9cb8 | 0x506870 | 0x10003fe34 | 0x3d0fc |
| MaskImageRideRect7 | 0x5da79c | 0x506ec8 | 0x100040630 | 0x3d92c |
| MaskImageRideRect8 | 0x5db190 | 0x507308 | 0x100040c0c | 0x3dee0 |
| AlphaBorderInnerRect | 0x5db974 | 0x507618 | 0x10004105c | 0x3e2f8 |
| AlphaBorderImageCoverRect | 0x5dbea0 | 0x507814 | 0x100041320 | 0x3e5bc |
| MaskImageEffectRect | 0x5dc9a4 | 0x507cfc | 0x1000419fc | 0x3ec20 |

The binary filenames, module initializer/registration mapping, common native
Layer acquisition and instance-reset/redraw lifecycle are recorded in
layerexalpha-maskrect-four-reference.md. The complete module uses that same
lifecycle. UTF-16 diagnostic strings were repaired from native bytes and all
affected methods re-decompiled. The native diagnostic helper is TVPAddLog,
not an exception. Pointer-width offsets, guard functions, exception cleanup
and register scheduling differ, not the shared algorithms.

## Pixel and color methods: shared pseudocode before implementation

All CPU loops visit width by height using the native pitch and preserve the
original full-image behavior. Most color methods explicitly reacquire the
destination writable buffer and pitch even when the active renderer is GPU.
They finish with the same virtual redraw; no script, generic canvas filter,
new image container or game-specific branch substitutes for these steps.

- turnAlpha: software writes (~pixel & 0xff000000). GPU compiles the cached
  zero-texture turnAlpha shader vec4(0,0,0,1), blend ADD,0,0,
  ONE_MINUS_DST_ALPHA,0; render the clip rectangle to a writable render texture
  with no source/reference textures.
- turnAlpha2: software pixel ^= 0xff000000. GPU has a separate cached
  turnAlpha2 method with RGB destination factor ONE; other factors equal the
  preceding method. CPU full-image and GPU clip-rectangle coverage differ
  in all four references; preserve both paths.
- turnAlpha3: force writable buffer, invert each alpha only when nonzero.
  Zero alpha stays zero; RGB is retained.
- turnOutline: force writable buffer; alpha 255 produces pixel zero,
  otherwise retain only its original alpha bits and clear RGB.
- AlphaColorRect(r,g,b): force writable buffer; write B/G/R independently,
  except a channel argument of -1 leaves that channel unchanged. No alpha
  write or extra range clamp exists.
- AlphaDivide(level): read the first pixel's alpha ONCE PER ROW; compare it
  with level; fill every pixel alpha in that row with 0 or 255. All four
  references agree on this unusual row-based behavior. Do not turn it into
  a per-pixel threshold.
- AlphaAdd(amount, nonTransparentOnly): force writable buffer; distinct
  branches either skip zero-alpha pixels or process all; clamp alpha+amount
  to [0,255].
- AlphaAdd2(amount, level): force writable buffer; compute original alpha+
  amount; values below zero or level become zero, otherwise cap at 255.
- BlackForAlpha: force writable buffer; set alpha=(B+G+R)/3, then clear RGB.
- AlphaBlend(amount): force writable buffer; alpha=int(float(amount)/255.f *
  float(alpha)), byte assignment without an additional clamp. Android32's
  clean decompile and native iOS64 FDIV/FMUL/FCVTZS instructions resolve
  Hex-Rays' erroneous mixed LOBYTE/LODWORD floating-register reconstruction.
- ColorAlphaAdd(rLimit,gLimit,bLimit,amount): force writable buffer; add amount
  to each RGB channel and cap only at its respective limit, with the original
  byte conversion; alpha=clamp(alpha-amount,0,255).
- ColorVectorEffectAdd(r1,r2,g1,g2,b1,b2,amount): skip zero alpha; subtract and
  clamp alpha, j=(255-newAlpha)/amount. Compute each channel independently as
  start + ((end-start)/(255/amount))*j and clamp to a byte. Integer divisions,
  channel order and truncation remain in that order.
- ExtraColorVectorEffectAdd has the same alpha/j step. With j nonzero and
  old RGB average greater than (r2+g2+b2)/3, compute those three bounded color
  expressions, write them, then preserve the redundant conditional rewrites
  when each new byte <= old average. Otherwise, B<=average takes r1,
  G<=average takes g1, R<=average takes b1. Select index as
  (G<R ? 1:2), overwritten by 2*(B<=R) when B>G; move that selected byte by
  +/-amount relative to the average, then store max(moved,average) as a byte.
  This asymmetric channel selection and the repeated stores are present in
  all four; do not substitute a visually similar gradient/filter.
- WhiteToBlueBlurAdd(amount): skip zero alpha; let sum=B+G+R and
  rate=((255-amount)/255.f)*.9f. For sum>590 set B=255; if alpha==255 also
  set G/R=255, otherwise multiply G/R by rate and truncate. Otherwise,
  when alpha==255 and B<sum/3, set B=255 and G/R=sum/3.
  Finally clamp original alpha-amount. Native iOS64 uses single-precision
  FDIV and FMUL; no inferred double-precision replacement is introduced.

Android64 uses branchless signed clamps and sometimes vectorizes RGB stores;
both 32-bit builds use conditional assignments/byte additions. iOS64 often
keeps counts in paired vector registers; iOS32 retains SjLj/FP spill scaffolding.
These are compiler/ABI differences. Their conditions, channel order, integer
divisions and the special row threshold above agree. No extra input guards or
clipping are inferred from method names.

The local layerExAlpha.cpp initially had only MaskRect. These members are
implemented in that same actual class, with direct writable-buffer/pitch calls,
the corresponding loops and cached render methods, and real NCB_METHOD
bindings. The supplied image/game material can validate its used paths;
there is no fabricated pixel fixture or blanket all-method runtime-oracle
claim. The remaining method groups were documented below before their source edits;
all 31 methods now have full bodies and actual native bindings.

## Extended masks and alpha lookup tables

ExMaskRect and ExMaskRect2 reject positions strictly greater than destination
width/height, logging Left and/or Top using the original <= diagnostic tests.
Clamp negative destination coordinates to zero, reduce requested extent by
the negative portion and cap the resulting right/bottom against destination
dimensions. For a negative destination y replace sy with -dy. The supplied sx
is unused; source alpha starts at clamped destination x, not supplied sx.
Keep this shared unusual source-coordinate calculation. Validate the source
native Layer, acquire its pitch then writable buffer. Blend cases 0,1,2,3 are
copy, saturated add, max(sum-255,0), multiply>>8; invalid mode logs and returns.
ExMaskRect clears rows above, both sides of every processed row, and below.
ExMaskRect2 clears both sides only and starts its destination pointer already
at clamped x; its subsequent left clearing therefore advances it by x again.
Do not silently repair that repeated offset or delegate to MaskRect.

The 32-bit ExMaskRect2 Hex-Rays output initially mislabeled the switch as
4*clampedDx and omitted type. Native disassembly proves both use the ninth
formal argument: Android32 loads frame arg_10 at 0x504dac, copies it to R6,
then TBB [PC,R6] at 0x504dd8; iOS32 loads arg_10 at 0x3adac, copies to R12,
then TBB [PC,R12] at 0x3add8. The byte-offset register is a different value.
The IDBs' signatures/comments were corrected and re-decompiled; this is a
decompiler representation defect, not an invented platform behavior difference.

LightForAlpha(source) validates Layer, reads its pitch and writable buffer,
then for each destination pixel copies source GREEN into destination alpha
and clears RGB. LuminanceForAlpha(source) instead sets alpha to truncation of
float(B)*.29891f + float(G)*.58661f + float(R)*.11448f, in that addition order,
then clears RGB. The reversed-looking B/R weighting is shared by all four.
These two methods consume the instance's existing destination buffer; they
do not add the force-writable destination step used by the other color methods.

LevelMaskRect(center,radius) forces destination writable, then resizes an actual
std::vector<int> to 2*radius+1 (Android32 explicitly names vector<int>::resize;
iOS64 helper 0x1000420fc and its three-pointer lifecycle match). For index i:
i<=radius uses int((255.f/(radius+1))*(i+1)+.5f); i==radius+1 is 255;
larger indices use int((255.f/(radius+1))*(count-i)+.5f). Pixels outside
[max(center-radius,0),min(center+radius,255)] get alpha zero; inside use the
table indexed by originalAlpha-(center-radius), including its unclamped base.
Keep the unusual second peak, vector lifetime and redraw before destruction.
LevelMaskRect2(center,leftRadius,rightRadius) resizes vector<int> to
leftRadius+rightRadius+1, uses a rising left slope for i<leftRadius, 255 at
the center, and the falling right slope otherwise; clamp table cells to 0..255.
Its pixel interval and unclamped table base use the corresponding asymmetric
radii. The four arithmetic paths agree; vector helpers, loop indexing and
destructor scaffolding differ by standard-library/ABI/compiler.

## Scalar source/mask transitions: shared source structure

The source dimensions queried by these methods are image dimensions
(iOS64 0x1000797a4/0x10007989c obtain MainImage and query its bitmap), while
the destination dimensions are the existing attached Layer bounds. The
multi-source methods acquire each native Layer, pitch, writable pixels, then
image width/height in that order. Mismatches log SW/SH, DW/DH, AW/AH and return
without redraw. Keep otherwise-unused image dimension queries where present.

Each lookup table is a separate native std::vector: coefficients are float,
color components int, or unsigned int specifically for MaskImageEffectRect.
Android32's named resize methods independently establish those element types;
all four share the same table counts, initialization and destruction order.
Per-pixel float products/rounding are retained. The clean 32-bit decompiles and
native floating instructions resolve 64-bit Hex-Rays' LOBYTE/LODWORD artifacts.

Common transition formulas (D=destination, S=source, M=mask alpha, C=constant
R/G/B, round(x)=int(float(x)+.5f), saturated table cells as in native):
- MaskImageRideRect(M,center,left,right,R,G,B): count=left+right+1; below table
  center i<left, coefficient=(left-i)/(left+1), colors=C*(i+1)/(left+1);
  at i==left coefficient=0, colors=C; above center coefficient=(i-left)/
  (right+1), colors=C*(count-i)/(right+1). The first red ramp uses double
  division and multiplication; green/blue coefficients are float, promoted to
  double for index multiplication before rounding. Apply only to nonzero D
  alpha and M in [max(center-left,0), center] or (center,min(center+right,255)).
  Table index M-(center-left); write each RGB as round(D*coef)+tableColor;
  keep D alpha.
- MaskImageRideRect2(M,center,radius,R,G,B): only a left table radius+1,
  same descending coefficient/rising colors and exact center. For nonzero
  D alpha, M>=max(center-radius,0) uses that table through center; M>center
  writes constant colors directly. Keep alpha.
- MaskImageRideRect3(M,center,radius,R,G,B): table radius+1, at index zero
  coefficient=0/colors=C, later coefficient=i/(radius+1) and colors=
  C*(count-i)/(radius+1). M<center writes constant colors; center through
  min(center+radius,255) uses the table. Keep alpha.
- MaskImageRideRect4(S,M,center,radius,R,G,B): validate equal dimensions;
  four tables of count=2*radius-1 centered at radius-1: coefficients fall
  to zero then rise; colors rise to C then fall, using divisor radius.
  low=(center-radius<0 ? 0 : center-radius+1), high=center+radius, each
  clamped to [0,255]. For D.a||S.a: M<low copies S RGB; low<=M<center
  combines S RGB with table colors (the original double intermediate path);
  center<=M<high combines D RGB with table colors (float path). Keep D alpha.
- MaskImageRideRect5(S,M,center,radius): equal dimensions; three float
  tables of count=2*radius-1: below midpoint sourceWeight=1-destWeight,
  destWeight=(radius-1-i)/radius, alphaWeight=1; at midpoint weights 1,0,1;
  above midpoint sourceWeight=1,destWeight=0,alphaWeight=(count-i)/radius.
  Clamp weights to [0,1]. For D.a||S.a, use the same low/high bounds:
  low<=M<center writes round(S*sourceWeight)+round(D*destWeight);
  center<=M<high copies S RGB and scales S alpha; M>=high clears D alpha.
  The native inner pixel loop uses TABLE COUNT 2*radius-1, not image width;
  all four show it. Preserve that surprising extent.
- MaskImageRideRect7(S,M,center,radius): equal dimensions; one float triangular
  table count=2*radius-1 with peak 1 at radius-1 and divisor radius.
  For D.a||S.a, low<=M<high copies S RGB and scales S alpha;
  M>=high clears alpha; lower masks leave destination unchanged.
- MaskImageRideRect8(S,M,level): equal dimensions. For D.a||S.a and M>level
  copy all four bytes from S; otherwise leave destination unchanged.
- AlphaBorderInnerRect(M,level): query M pitch, writable bytes and image dims;
  only width is validated. On matching width, clear D alpha where M>=level,
  preserving RGB; redraw. Height is queried but not part of that gate.
- AlphaBorderImageCoverRect(S,A,M,center,radius): acquire S/A/M in order.
  Gate sizes on S and M only; A dimensions are still queried then unused.
  Reject radius<1, center+radius<1 or center-radius>254 with Out of range log.
  Compute left/right cropped radii using the original center<0, center>=radius,
  center>=256 and center+radius>=256 branches; clamp each cropped radius >=0.
  A float coefficient vector of count=left+right+1 rises from
  (radius-left+i)/radius, center=1, then falls from (radius+left+1-i)/radius;
  cap to [0,1]. For D.a||S.a and M in [center-left,center+right), copy S RGB
  and set D alpha=round(A.a*coefficient). No further alpha normalization.
- MaskImageEffectRect(M,center,left,right,R,G,B): one float and FOUR unsigned
  tables (alpha,R,G,B), count=left+right+1. Coefficient/color ramps follow
  MaskImageRideRect, but left red uses INTEGER (R/(left+1))*(i+1) before float
  rounding. Alpha ramps to 255 on the left and remains 255 on/right of center.
  Unsigned table clamps retain the original negative-conversion behavior and
  redundant zero stores. For nonzero D.a: M<max(center-left,0) clears alpha;
  M<=center writes RGB plus table colors AND table alpha; center<M<upper writes
  RGB only. Others leave destination unchanged.

These are direct CPU buffer operations; no generic GPU shader substitute,
different blend equation, array-of-structs table, merged table allocation or
new clipping policy is inserted. The native stale-buffer and unchecked-input
boundaries remain visible rather than hidden behind game-specific defaults.


## Local implementation correspondence and validation scope

Each method above has an identically named direct member in
`cpp/plugins/layerExAlpha.cpp` and one `NCB_METHOD` in the same native module.
No registered method is a success stub, alias or script shim. The NCB instance
hook obtains/creates the Layer-owned native adaptor and resets the original
base information before every call; redraw uses the inherited clip update.

The source keeps one explicit control-flow body per method. Source/alpha/mask
objects are acquired in their original order, including writable source access
and unused dimension queries. Tables are independently declared and resized
with their original element types, and redraw occurs before reverse-order
vector destruction. Pixel loops preserve the documented source coordinate,
interval, rounding, alpha gating and unusual iteration details. GPU methods
retain their separate cached render methods, source arrays and blend factors.

The module builds in the Web release configuration, and the required source
checks pass. Runtime verification uses the existing complete game packages;
it covers the actual alpha-mask transition that previously failed, native game
save/load and input, and registration of every exposed member. No existing
pixel oracle exercises all 31 algorithms or every renderer/input combination.
Those unused paths have four-reference implementation evidence and build
coverage, but are not claimed as independently pixel-validated.
