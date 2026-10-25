# Web font substitution and text cells

The Web core cannot assume Windows fonts are installed. When a requested
`宋体` / `SimSun` family is unavailable, it uses the bundled static **Noto Sans
SC Regular** as an explicit substitute. This is an open Chinese font, not a
claim of SimSun appearance or metric parity. Other missing families retain the
existing default-font behavior.

Real registered families, including fonts registered later through
`System.addFont`, take priority. Available explicit candidates are checked in
their original order before loading a substitute. The existing leading-empty
forced-family convention, vertical-family marker and `force_default_font`
setting retain their meanings. The substitute is loaded on demand and does not
replace the selected default font.

## Cell height and baseline

Some callers allocate a temporary Layer of `font.getTextHeight(text)` pixels
and draw at its origin. Nominal EM size and vertical-writing advance do not
describe the physical height of that text cell.

For Web FreeType fonts, measurement uses the scaled ascent minus descent,
at least the requested pixel size. Bitmap-backed and standalone Font objects
use the same measurement. Actual prerendered items retain their existing
nominal height; missing items use the rasterizer's measured height. Escapement
height uses the same result. `font.height`, glyph pixels and horizontal
advances remain independent of this physical cell height.

Web layout ascent is rounded upward to an integral pixel before rasterization.
Hinting can round a top bearing upward too; truncating the baseline can
otherwise discard a top scanline at a valid caller origin. This is one baseline
per font size, not a per-character translation. Caller clipping is still
honored. These changes do not promise that every font's ink fits every
arbitrary caller rectangle.

References:

- [FreeType glyph metrics and the effects of grid fitting](https://freetype.org/freetype2/docs/glyphs/glyphs-3.html)
- [OpenType baseline tags](https://learn.microsoft.com/en-us/typography/opentype/spec/baselinetags)
- [KiriKiri Font.getTextHeight](https://krkrz.github.io/krkr2doc/kr2doc/contents/f_Font_getTextHeight.html)
- [KiriKiri Layer.drawText](https://krkrz.github.io/krkr2doc/kr2doc/contents/f_Layer_drawText.html)

## Bundled font provenance

The upstream font is included unmodified with its SIL Open Font License 1.1:

- Repository: [notofonts/noto-cjk](https://github.com/notofonts/noto-cjk/tree/f8d157532fbfaeda587e826d4cd5b21a49186f7c)
- Fixed commit: `f8d157532fbfaeda587e826d4cd5b21a49186f7c`
- [Font source](https://raw.githubusercontent.com/notofonts/noto-cjk/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf), 8,331,336 bytes
- Font SHA-256: `faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9`
- [License source](https://raw.githubusercontent.com/notofonts/noto-cjk/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/LICENSE), 4,301 bytes
- License SHA-256: `6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2`

Both files ship in `assets.zip`. Release verification checks their sizes,
uniqueness and digests; the existing 16 MiB assets budget is unchanged.

## Focused regression checks

`test-font-candidates.py` exercises the production selector, candidate ordering,
real-family precedence, lazy substitution and explicit default settings.
`test-font-baseline.py` checks actual hinted glyphs and shared bitmap/rectangle
baselines. `test-font-cell-height.py` runs production FreeType measurement,
bitmap allocation, glyph construction, blur and clipping paths against real
font data, counting excluded alpha pixels rather than only bounding boxes.
It also retains a control for the original bundled font's descenders and
outlined text, plus native and prerendered-font behavior.
