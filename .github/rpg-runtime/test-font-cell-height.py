#!/usr/bin/env python3
"""Compile the production font measurement/bitmap clip seams with real FreeType."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--font", type=Path, default=ROOT / "ui/cocos-studio/NotoSansSC-Regular.otf")
args = parser.parse_args()


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def member(source, signature, owner):
    return function(source, signature).replace(owner + "::", "", 1)


header = (ROOT / "cpp/core/visual/FreeType.h").read_text()
face = (ROOT / "cpp/core/visual/FreeType.cpp").read_text()
raster = (ROOT / "cpp/core/visual/FreeTypeFontRasterizer.cpp").read_text()
bitmap = (ROOT / "cpp/core/visual/impl/LayerBitmapImpl.cpp").read_text()
layer = (ROOT / "cpp/core/visual/LayerIntf.cpp").read_text()
character = (ROOT / "cpp/core/visual/CharacterData.cpp").read_text()
blend = (ROOT / "cpp/core/visual/gl/blend_function.cpp").read_text()
functors = (ROOT / "cpp/core/visual/gl/blend_functor_c.h").read_text()
tvpgl = (ROOT / "cpp/core/visual/tvpgl.cpp").read_text()
constructor = function(face, "tFreeTypeFace::tFreeTypeFace(")
initialization = re.findall(r"\bLayoutAscent\s*=[^;]+;", constructor)
assert len(initialization) == 1
line_height = re.search(r"(?:\[\[nodiscard\]\]\s*)?tjs_int GetLineHeight\(\) const", header)
# Old production has no GetLineHeight. Do not invent one: its real old
# GetTextExtent/GetTextSize still compile and fail on measured lost ink.
optional_line_height = function(header, line_height[0]) if line_height else ""
face_methods = "\n".join([
    function(header, "tjs_int GetHeight()"),
    function(header, "[[nodiscard]] tjs_int GetAscent() const"),
    function(header, "void GetUnderline("),
    function(header, "void GetStrikeOut("),
    optional_line_height,
    *[member(face, signature, "tFreeTypeFace") for signature in (
        "void tFreeTypeFace::SetHeight(",
        "bool tFreeTypeFace::LoadGlyphSlotFromCharcode(",
        "bool tFreeTypeFace::GetGlyphMetricsFromCharcode(",
        "bool tFreeTypeFace::GetGlyphSizeFromCharcode(",
        "tTVPCharacterData *tFreeTypeFace::GetGlyphFromCharcode(",
    )],
])
flags = "\n".join(re.findall(r"^#define TVP_FACE_OPTIONS_(?:NO_HINTING|FORCE_AUTO_HINTING|NO_ANTIALIASING)[\s\S]*?(?=\n[^\s]|\Z)", header, re.M))
flags = flags.replace("\\\n", " ")
font_flags = "\n".join(re.findall(r"^#define TVP_TF_\w+[^\n]*", (ROOT / "cpp/core/visual/tvpfontstruc.h").read_text(), re.M))
blur_code = "\n".join([
    "template <int tmax>\n" + function(functors, "struct ch_blur_mul_copy_xx_functor") + ";",
    "template <int tmax>\n" + function(functors, "struct ch_blur_add_mul_copy_xx_functor") + ";",
    "using ch_blur_add_mul_copy_functor = ch_blur_add_mul_copy_xx_functor<255>;",
    "using ch_blur_add_mul_copy65_functor = ch_blur_add_mul_copy_xx_functor<64>;",
    "template <typename functor>\n" + function(blend, "static inline void blend_func_c(tjs_uint8"),
    function(tvpgl, "tjs_uint fast_int_hypot("),
    "template <typename functor>\n" + function(blend, "static inline void ch_blur_copy_func("),
    function(blend, "void TVP_ch_blur_copy65(").replace("TVP_ch_blur_copy65", "TVPChBlurCopy65", 1),
    function(blend, "void TVP_ch_blur_copy(").replace("TVP_ch_blur_copy", "TVPChBlurCopy", 1),
])

prelude = r'''
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_BITMAP_H
#include FT_SYNTHESIS_H
#include "cpp/core/visual/FontBaseline.h"
using tjs_int = int;
using tjs_int32 = std::int32_t;
using tjs_uint = unsigned;
using tjs_uint32 = std::uint32_t;
using tjs_uint8 = std::uint8_t;
using tjs_char = char16_t;
#define TJS_W(value) u##value
class ttstr {
    std::u16string value;
public:
    ttstr() = default;
    ttstr(const char16_t *text): value(text) {}
    const char16_t *c_str() const { return value.c_str(); }
    bool operator!=(const ttstr &other) const { return value != other.value; }
    void Clear() { value.clear(); }
    std::string AsStdString() const { return "real-font-fixture"; }
};
struct tTVPFont { int Height = 0; unsigned Flags = 0; int Angle = 0; };
struct tGlyphMetrics { int CellIncX = 0, CellIncY = 0; };
void *TJSAlignedAlloc(int bytes, int) {
    void *data = std::malloc(bytes);
    if(!data) throw std::bad_alloc();
    return data;
}
void TJSAlignedDealloc(void *data) { std::free(data); }
[[noreturn]] void TVPThrowExceptionMessage(const char16_t *) {
    throw std::runtime_error("unsupported glyph operation in the font seam");
}
'''
character_declaration = r'''
void TVPChBlurMulCopy(tjs_uint8 *dest, const tjs_uint8 *src, int count, int level) {
    ch_blur_mul_copy_xx_functor<255> operation(level);
    blend_func_c(dest, src, count, operation);
}
void TVPChBlurMulCopy65(tjs_uint8 *dest, const tjs_uint8 *src, int count, int level) {
    ch_blur_mul_copy_xx_functor<64> operation(level);
    blend_func_c(dest, src, count, operation);
}
class tTVPCharacterData {
public:
    int OriginX = 0, OriginY = 0, Pitch = 0, RefCount = 1;
    unsigned BlackBoxX = 0, BlackBoxY = 0, Gray = 65;
    bool Antialiased = false, Blured = false, FullColored = false;
    int BlurLevel = 0, BlurWidth = 0;
    tGlyphMetrics Metrics;
    tjs_uint8 *Data = nullptr;
    tTVPCharacterData(const tjs_uint8 *, int, int, int, unsigned, unsigned,
                      const tGlyphMetrics &, bool = false);
    ~tTVPCharacterData();
    tjs_uint8 *GetData() const { return Data; }
    void Blur(int blurlevel, int blurwidth);
    void AddHorizontalLine(int, int, int) {
        throw std::runtime_error("decorations are outside the cell-height seam");
    }
};
'''
face_declaration = r'''
class tFreeTypeFace {
public:
    FT_Face FTFace;
    int LayoutAscent, Height = 0;
    unsigned Options = 0;
    ttstr FontName;
    unsigned (*UnicodeToLocalChar)(tjs_char) = nullptr;
    explicit tFreeTypeFace(FT_Face face): FTFace(face), LayoutAscent(face->ascender) {
''' + "\n".join(initialization) + r'''
    }
    static int FT_PosToInt(int value) { return (value + 32) >> 6; }
''' + face_methods + "\n};\n"
raster_declaration = r'''
class FreeTypeFontRasterizer {
public:
    tFreeTypeFace *Face;
    explicit FreeTypeFontRasterizer(tFreeTypeFace &face): Face(&face) {}
    void ApplyFont(const tTVPFont &font) { Face->SetHeight(std::abs(font.Height)); }
''' + member(raster, "void FreeTypeFontRasterizer::GetTextExtent(", "FreeTypeFontRasterizer") + "\n};\n"
bitmap_declaration = r'''
static FreeTypeFontRasterizer *currentRasterizer = nullptr;
FreeTypeFontRasterizer *GetCurrentRasterizer() { return currentRasterizer; }
struct tTVPPrerenderedCharacterItem { int Inc = 0; };
struct PrerenderedFontFixture {
    // Only the stored-font lookup boundary is supplied here. Production
    // GetTextSize decides whether a mapped item or real FreeType is measured.
    tTVPPrerenderedCharacterItem item{44};
    const tTVPPrerenderedCharacterItem *Find(tjs_char ch) {
        return ch == u'中' ? &item : nullptr;
    }
};
struct tTVPRect {
    int left, top, right, bottom;
    tTVPRect(): left(0), top(0), right(0), bottom(0) {}
    tTVPRect(int l, int t, int r, int b): left(l), top(t), right(r), bottom(b) {}
};
struct tTVPDrawTextData { tTVPRect rect; };
class tTVPNativeBaseBitmap {
public:
    tTVPFont Font;
    ttstr CachedText;
    int TextWidth = 0, TextHeight = 0;
    PrerenderedFontFixture *PrerenderedFont = nullptr;
    FreeTypeFontRasterizer *rasterizer;
    int width = 0, height = 0;
    std::uint64_t copiedNonzero = 0;
    tTVPNativeBaseBitmap(FreeTypeFontRasterizer &raster, int size): rasterizer(&raster) {
        Font.Height = -size;
    }
    void ApplyFont() { currentRasterizer = rasterizer; rasterizer->ApplyFont(Font); }
    unsigned GetWidth() const { return width; }
    unsigned GetHeight() const { return height; }
    bool InternalBlendText(tTVPCharacterData *data, tTVPDrawTextData *, unsigned,
                           const tTVPRect &source, tTVPRect &) {
        // The renderer boundary observes exactly the production clip's source.
        for(int y = source.top; y < source.bottom; ++y)
            for(int x = source.left; x < source.right; ++x)
                copiedNonzero += data->GetData()[y * data->Pitch + x] != 0;
        return true;
    }
''' + "\n".join(member(bitmap, signature, "tTVPNativeBaseBitmap") for signature in (
    "void tTVPNativeBaseBitmap::GetTextSize(",
    "tjs_int tTVPNativeBaseBitmap::GetTextHeight(",
    "tjs_int tTVPNativeBaseBitmap::GetTextWidth(",
    "bool tTVPNativeBaseBitmap::InternalDrawText(",
)) + "\n};\n"
font_declaration = r'''
class tTJSNI_Font {
public:
    tTVPNativeBaseBitmap *Layer = nullptr;
    tTVPFont Font;
''' + member(layer, "tjs_int tTJSNI_Font::GetTextHeight(", "tTJSNI_Font") + "\n};\n"

scratch_root = ROOT / "out/font-validation"
scratch_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch_root) as directory:
    scratch = Path(directory)
    generated = prelude + flags + "\n" + font_flags + "\n" + blur_code
    generated += character_declaration + "\n" + "\n".join(function(character, signature) for signature in (
        "tTVPCharacterData::tTVPCharacterData(const tjs_uint8",
        "tTVPCharacterData::~tTVPCharacterData()",
        "void tTVPCharacterData::Blur(tjs_int blurlevel",
    ))
    generated += face_declaration + raster_declaration + bitmap_declaration + font_declaration
    (scratch / "production-font-cell.h").write_text(generated)
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "freetype2"], text=True))
    for label, defines in (("native", []), ("web", ["-D__EMSCRIPTEN__"])):
        executable = scratch / ("font-cell-height-" + label)
        print("font cell measurement mode: " + label, flush=True)
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", *defines, "-I", str(ROOT), "-I", str(scratch),
            str(ROOT / "tests/web/font_cell_height.test.cpp"), "-o", str(executable), *flags,
        ], check=True)
        subprocess.run([str(executable), str(args.font.resolve()),
                        str(ROOT / "ui/cocos-studio/DroidSansFallback.ttf")], check=True)
