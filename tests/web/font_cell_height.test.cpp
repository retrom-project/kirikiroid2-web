#include "production-font-cell.h"
#include <iostream>

static void require(bool condition, const char *message) {
    if(!condition) throw std::runtime_error(message);
}

#ifdef __EMSCRIPTEN__
static std::uint64_t nonzero(const tTVPCharacterData &data) {
    std::uint64_t count = 0;
    for(unsigned y = 0; y < data.BlackBoxY; ++y)
        for(unsigned x = 0; x < data.BlackBoxX; ++x)
            count += data.GetData()[y * data.Pitch + x] != 0;
    return count;
}

static std::uint64_t pixels(const tTVPCharacterData &data) {
    std::uint64_t checksum = 14695981039346656037ull;
    for(unsigned y = 0; y < data.BlackBoxY; ++y)
        for(unsigned x = 0; x < data.BlackBoxX; ++x)
            checksum = (checksum ^ data.GetData()[y * data.Pitch + x]) * 1099511628211ull;
    return checksum;
}

static std::uint64_t excluded(tTVPNativeBaseBitmap &scratch,
                              tTVPCharacterData &data, int x, int y) {
    tTVPDrawTextData draw{{0, 0, scratch.width, scratch.height}};
    tTVPRect destination;
    scratch.copiedNonzero = 0;
    scratch.InternalDrawText(&data, x, y, 0, &draw, destination);
    const auto total = nonzero(data);
    require(scratch.copiedNonzero <= total, "clip copied nonexistent alpha samples");
    return total - scratch.copiedNonzero;
}
#endif

static FT_Face open(FT_Library library, const char *path) {
    FT_Face face = nullptr;
    require(!FT_New_Face(library, path, 0, &face), "bundled static font did not open");
    require(!FT_HAS_MULTIPLE_MASTERS(face), "cell regression requires a fixed static face");
    return face;
}

#ifdef __EMSCRIPTEN__
static void checkMeasuredScratch(FT_Library library, const char *path) {
    FT_Face face = open(library, path);
    tFreeTypeFace driver(face);
    driver.SetHeight(44);
    FreeTypeFontRasterizer raster(driver);
    currentRasterizer = &raster;
    tTVPNativeBaseBitmap bitmap(raster, 44);
    tTJSNI_Font standalone;
    standalone.Font.Height = -44;
    tTJSNI_Font attached;
    attached.Layer = &bitmap;
    attached.Font.Height = -44;
    unsigned clipped = 0;
    unsigned measurementFailures = 0;
    // Public font clients use CJK, punctuation and Latin, independently of any game.
    for(tjs_char ch : std::u16string(u"中文全学，。！？Agpy \u3000")) {
        const char16_t text[] = {ch, 0};
        auto original = std::unique_ptr<tTVPCharacterData>(driver.GetGlyphFromCharcode(ch));
        require(original != nullptr, "generic test glyph is absent from static face");
        const auto originalPixels = pixels(*original);
        const int baseline = driver.GetAscent();
        const auto originalAdvance = original->Metrics.CellIncX;
        bitmap.CachedText.Clear();
        int width = 0, cellHeight = 0;
        raster.GetTextExtent(ch, width, cellHeight);
        const int bitmapHeight = bitmap.GetTextHeight(text);
        const int attachedHeight = attached.GetTextHeight(text);
        const int standaloneHeight = standalone.GetTextHeight(text);
        // Independent FreeType size metrics are the caller's measured cell,
        // not a vertical writing advance or the requested EM scalar.
        const int expectedCell = std::max(44, static_cast<int>(
            (face->size->metrics.ascender - face->size->metrics.descender) / 64));
        measurementFailures += cellHeight != expectedCell || bitmapHeight != expectedCell ||
            attachedHeight != bitmapHeight || standaloneHeight != bitmapHeight;
        require(width == originalAdvance && bitmap.GetTextWidth(text) == width,
                "height measurement changed the actual glyph advance");
        for(int edge : {0, 3}) {
            auto image = std::unique_ptr<tTVPCharacterData>(driver.GetGlyphFromCharcode(ch));
            if(edge) {
                image->BlurLevel = 4096;
                image->Blur(4096, edge);
            }
            tTVPNativeBaseBitmap scratch(raster, 44);
            // A conventional gradient scratch adds horizontal EM padding and
            // measures height, then draws at its outline/offset padding.
            scratch.width = width + 44 + edge;
            scratch.height = bitmapHeight + edge;
            const auto lost = excluded(scratch, *image, edge, edge);
            std::cout << "static char=" << std::hex << unsigned(ch) << std::dec
                      << " requested=44 baseline=" << baseline << " extent=" << width << '/'
                      << cellHeight << " height=" << bitmapHeight << '/' << standaloneHeight
                      << " edge=" << edge << " scratch=" << scratch.width << 'x' << scratch.height
                      << " excludedNonzero=" << lost << std::endl;
            clipped += lost != 0;
        }
        require(bitmap.Font.Height == -44 && standalone.Font.Height == -44 &&
                    face->size->metrics.y_ppem == 44 && driver.GetAscent() == baseline,
                "measurement rescaled nominal font size or changed the shared baseline");
        auto after = std::unique_ptr<tTVPCharacterData>(driver.GetGlyphFromCharcode(ch));
        require(after && pixels(*after) == originalPixels &&
                    after->Metrics.CellIncX == originalAdvance,
                "measurement changed rasterized glyph pixels or advance");
    }
    bitmap.CachedText.Clear();
    const int oneCell = bitmap.GetTextHeight(u"中");
    require(bitmap.GetTextHeight(u"中文Ag") == oneCell &&
                standalone.GetTextHeight(u"中文Ag") == oneCell,
            "multi-character measurement added cell heights instead of taking their maximum");
    standalone.Font.Height = 44;
    require(standalone.GetTextHeight(u"中文Ag") == oneCell && standalone.Font.Height == 44,
            "positive nominal font height changed the measured cell or caller's font state");
    FT_Done_Face(face);
    require(!clipped, "measured scratch clipped real glyph/outline alpha");
    require(!measurementFailures, "public font measurement returned nominal EM/vertical advance instead of cell height");
}

static void checkExistingBody(FT_Library library, const char *path) {
    FT_Face face = open(library, path);
    tFreeTypeFace driver(face);
    driver.SetHeight(40);
    FreeTypeFontRasterizer raster(driver);
    for(tjs_char ch : std::u16string(u"中文，。Ag")) {
        auto glyph = std::unique_ptr<tTVPCharacterData>(driver.GetGlyphFromCharcode(ch));
        require(glyph != nullptr, "bundled CJK control glyph missing");
        tTVPNativeBaseBitmap scratch(raster, 40);
        scratch.width = 1288;
        scratch.height = 58;
        require(!excluded(scratch, *glyph, 4, 14), "existing body glyph clips after cell change");
        glyph->Blur(4096, 3);
        require(!excluded(scratch, *glyph, 4, 14), "existing body outline/descender clips after cell change");
    }
    FT_Done_Face(face);
    std::cout << "bundled 40px/y14/58 body and outline control passed\n";
}

static void checkStoredFontFallback(FT_Library library, const char *path) {
    FT_Face face = open(library, path);
    tFreeTypeFace driver(face);
    driver.SetHeight(44);
    FreeTypeFontRasterizer raster(driver);
    tTVPNativeBaseBitmap bitmap(raster, 44);
    PrerenderedFontFixture mapped;
    bitmap.PrerenderedFont = &mapped;
    require(bitmap.GetTextHeight(u"中") == 44 && bitmap.GetTextWidth(u"中") == 44,
            "stored font item lost its nominal measurement");
    const int cellHeight = static_cast<int>((face->size->metrics.ascender -
                                             face->size->metrics.descender) / 64);
    require(bitmap.GetTextHeight(u"文") == cellHeight,
            "missing stored item did not propagate its actual FreeType cell height");
    require(bitmap.GetTextHeight(u"中文") == cellHeight && bitmap.GetTextWidth(u"中文") == 88,
            "mixed stored/native measurement failed to keep maximum height and advances");
    FT_Done_Face(face);
    std::cout << "stored item nominal height and real FreeType fallback passed\n";
}
#else
static void checkNativeUnchanged(FT_Library library, const char *path) {
    FT_Face face = open(library, path);
    tFreeTypeFace driver(face);
    driver.SetHeight(44);
    FreeTypeFontRasterizer raster(driver);
    currentRasterizer = &raster;
    tTVPNativeBaseBitmap bitmap(raster, 44);
    tTJSNI_Font standalone;
    standalone.Font.Height = -44;
    require(bitmap.GetTextHeight(u"中文Ag") == 44 &&
                standalone.GetTextHeight(u"中文Ag") == 44,
            "Web cell measurement changed the native nominal-height contract");
    require(driver.GetAscent() == face->ascender * 44 / face->units_per_EM,
            "Web ascent rounding changed the native baseline");
    FT_Done_Face(face);
    std::cout << "native font height and baseline control passed\n";
}
#endif

int main(int argc, char **argv) {
    try {
        require(argc == 3, "static and default font paths required");
        FT_Library library = nullptr;
        require(!FT_Init_FreeType(&library), "FreeType unavailable");
#ifdef __EMSCRIPTEN__
        checkExistingBody(library, argv[2]);
        checkMeasuredScratch(library, argv[1]);
        checkStoredFontFallback(library, argv[1]);
#else
        checkNativeUnchanged(library, argv[1]);
#endif
        FT_Done_FreeType(library);
        std::cout << "real font cell measurement and scratch clipping passed\n";
    } catch(const std::exception &error) {
        std::cerr << "font cell measurement regression: " << error.what() << '\n';
        return 1;
    }
}
