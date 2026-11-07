# GlitchEffect.dll (game-supplied Layer extension)

## Four-reference status

| Binary | Result |
| --- | --- |
| Kirikiroid2_1.3.9_Android_arm64-v8a.so | absent: no ASCII `glitchCopy`/`doGlitch`/`GlitchEffect`/`glitch`, no UTF-16LE `glitch` |
| Kirikiroid2_1.3.9_Android_armabi-v7a.so | absent (same searches) |
| Kirikiroid2_1.3.9_iOS_arm64 | absent (same searches) |
| Kirikiroid2_1.3.9_iOS_armv7 | absent (same searches) |

The module is therefore a Web extension compiled with the other Web-only plugins.
Its behavior evidence is the supplied PE32 plugin
(SHA-256 `d9192959a05023d5266e1bfc2a735c9a8bd213cb557fd28c2107bf6807734713`, image base `0x10000000`).

## Supplied plugin mapping (PE32 addresses)

| Function | Role |
| --- | --- |
| `sub_10003220` | V2Link registration: `Layer.glitchCopy` → `sub_100040E0`, `Layer.doGlitch` → `sub_10002FB0` |
| `sub_10005D50` | SimpleBinder invoke: `numparams < count` → `-1004`, else forwards parameter pointers |
| `sub_100040E0` | glitchCopy: `src`/`elm` must be objects (`-1003`), clears result, prepare, apply |
| `sub_10002FB0` | doGlitch: same with dx/dy/sx/sy = 0 and source-sized w/h |
| `sub_100043E0` | prepare: source rect ∩ source image, destination rect ∩ destination clip; stores the destination clip rect; source descriptor at original (sx, sy); duplicate when destination == source |
| `sub_10004580` | rectangle intersection, clears on empty |
| `sub_100034E0` | destination clip rect from clipLeft/Top/Width/Height |
| `sub_10003B30` | `hasImage`, positive `imageWidth`/`imageHeight` |
| `sub_10003A10` | pitch: flag 1 → `mainImageBufferPitch`, else `provinceImageBufferPitch` |
| `sub_10003BD0` / `sub_10003C70` | `mainImageBuffer` / `mainImageBufferForWrite` |
| `sub_10005D00` | source descriptor {base, min, max, pitch} |
| `sub_10002C60` | duplicate the descriptor range and rebase |
| `sub_10004B00` | apply: params, `size` (≤0 → 16), seed (`seed` or `TVPGetTickCount()`), two SplitMix64 values, fill rows, fill blocks, `coef` (1.0), render |
| `sub_10001AB0` | params: noise 4, sft_x 16, sft_y 8, sft_col 8, per_x 0.5, per_y 0.25, per_col 0.05, per_reset 0.02 |
| `sub_10003FA0` / `sub_10001FE0` / `sub_100042D0` | dictionary real/int/present (failed PropGet or void → default) |
| `sub_10002420` | field: copies params, SplitMix(0) state, vectors rows (h), mask ((h+63)>>6), three cells (cols×rows) |
| `sub_10005C50` | SplitMix64 |
| `sub_10005BA0` | xoroshiro128** step |
| `sub_10002980` | polar Box–Muller, returns factor·y, caches factor·x (`C = -2.0` at `0x10016798`) |
| `sub_10005FC0` | row offsets = normal()·noise + 0 |
| `sub_10005DC0` / `sub_10002250` | three running walks (x/y/col): jump with per_*, σ = sft_*; reset with per_reset (×0.0); fresh Box–Muller cache per walk |
| `sub_100048F0` | render: head segment of `left % size`, full blocks, tail; absolute row/cell indexes |
| `sub_10004F40` | segment copy with clamped source; colour split by `shift & 3` at ±4·shift pixels |

## Transitions (PE32 addresses)

`V2Link` → `sub_10006030`: register `glitch` (`sub_100053F0`), `fadeglitch` (`sub_10005310`),
`loopglitch` (`sub_10005380`), then the Layer methods (`sub_10003220(1)`). `V2Unlink` →
`sub_10003130(0)`: `TVPRemoveTransHandlerProvider` + Release for each, then the methods.

| Item | glitch (`TransGlitch`) | fadeglitch (`FadeGlitch`) | loopglitch (`LoopGlitch`) |
| --- | --- | --- | --- |
| provider vtable | `0x10016660` | `0x10016674` | `0x10016688` |
| type / update / same size | ttExchange / tutDivisibleFade / yes | ttSimple / tutDivisibleFade / no | ttSimple / tutDivisibleFade / no |
| StartTransition | `sub_100059B0` | `sub_10005870` | `sub_10005910` |
| create | `sub_10002E90` | `sub_10002CA0` | `sub_10002DF0` |
| ctor | `sub_10002740` | `sub_10002340` | `sub_10002670` |
| handler vtable | `0x10016294` | `0x10016310` | `0x10016380` |
| update (slot 8) | `sub_10005730` | `sub_10005540` | `sub_100055D0` |
| draw (slot 9) | `sub_10004DA0` | `sub_100047A0` | `sub_10004D70` |

StartTransition fails without a handler slot or options (and, for glitch, on unequal sizes).
It writes type/update type and calls create, failing if create returns null.

- Options (`sub_10001CD0` int / `sub_10001DD0` real / `sub_10001EE0` int64): `options->GetValue`; a failed
  or void value keeps the default. Effect params `sub_10001BC0` use the same defaults as the Layer path.
- create: `time` (default -1; negative → null; < 2 → 2), params, `block` (16, ≤0 → 16).
  glitch: `gamma_in`/`gamma_out` (1.0). fadeglitch: `nofade`, `fadein`, `gamma_in` when fadein else
  `gamma_out`, `color` (0; nofade → 0x808080). loopglitch: params and block only; time = -1.
- Shared slots (`TransGlitchBase` `0x10016268`): AddRef `sub_10002BF0`, Release `sub_100054F0`, SetOption
  `sub_10004F30` (loop: `sub_10005520` stores the provider), StartProcess `sub_100057A0`,
  EndProcess `sub_10003110` (TRUE), Process `sub_10004E10`, MakeFinalImage `sub_10004790` (`*dest = src2`).
- StartProcess: first frame (progress < 0) stores the tick; signed 64-bit `elapsed >= time` → FALSE;
  else progress = elapsed / time, update(tick), TRUE. LoopGlitch (time -1) therefore ends at its first frame.
- update: reseed xoroshiro with two SplitMix64 of the tick, fill rows and blocks. glitch also fills a per-row
  64-bit mask (`sub_10005EF0`: bit set when progress > uniform()). fadeglitch first sets the
  colour (`sub_10003710`, each channel `c·w + (1−w)·128`, w = q², q = fadein ? 1−p : p). loopglitch first reads
  `coef`/`break` from the stored option provider.
- Process (`sub_10003800`, `sub_10002580`): dest = scanline(DestTop) + 4·DestLeft. Both sources come from row 0
  with their full width/height bounds, and rect = {Left, Top, Left+Width, Top+Height}.
- draw: glitch `sub_10004830`: masked rows take src2 with pow(1−p, gamma_out), others src1 with
  pow(p, gamma_in), via the row function `sub_10005160`. fadeglitch: render (`sub_100048F0`) src1 (src2 when fadein) with
  pow(q, gamma), then, when colour ≠ 0x808080, the overlay `sub_100033F0`. loopglitch: render src1 with `coef`.

## Platform boundary

The plugin forms source addresses before clamping; the port uses offsets with the same clamp.
Row/cell tables are indexed by absolute destination coordinates while sized by the clip
extent; an index outside the tables (non-zero clip origin) is undefined in the original and
reads 0.0 here. No existing fixture exercises the plugin, so verification is the real
product run of a game that links it.

Transitions read and write scanlines through the local texture interface (`GetTexture()->GetScanLineForRead`,
`GetTextureForRender()->GetScanLineForWrite`, `GetPitch`) instead of the Windows provider's
`GetScanLine`/`GetPitchBytes`. A simple transition without a source layer has no Src2. The plugin dereferences it;
the port returns `TJS_E_FAIL` instead. The mask is indexed by absolute row; rows outside it read as unmasked.
