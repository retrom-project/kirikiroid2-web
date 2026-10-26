#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include "production-font-baseline.h"

static void require(bool condition, const char *message) {
    if(!condition) throw std::runtime_error(message);
}

struct BitmapFacts {
    unsigned width, rows;
    int left, top;
    FT_Pos advance;
    std::uint64_t pixels;
    bool operator==(const BitmapFacts &other) const {
        return width == other.width && rows == other.rows && left == other.left &&
            top == other.top && advance == other.advance && pixels == other.pixels;
    }
};

static BitmapFacts bitmap(FT_Face face) {
    std::uint64_t checksum = 14695981039346656037ull;
    const auto &image = face->glyph->bitmap;
    for(unsigned y = 0; y < image.rows; ++y)
        for(unsigned x = 0; x < image.width; ++x)
            checksum = (checksum ^ image.buffer[y * image.pitch + x]) * 1099511628211ull;
    return {image.width, image.rows, face->glyph->bitmap_left,
            face->glyph->bitmap_top, face->glyph->advance.x, checksum};
}

static FT_Face open(FT_Library library, const char *path, unsigned size) {
    FT_Face face = nullptr;
    require(!FT_New_Face(library, path, 0, &face), "real font failed to open");
    require(!FT_Set_Pixel_Sizes(face, 0, size), "nominal EM size failed");
    return face;
}

static void render(FT_Face face, unsigned ch) {
    require(!FT_Load_Char(face, ch, FT_LOAD_NO_BITMAP), "actual glyph failed to load");
    require(!FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL), "actual glyph failed to render");
}

static void checkNameField(FT_Library library, const char *path, const char *label) {
    FT_Face face = open(library, path, 120);
    ProductionFace driver(face);
    for(unsigned ch : {0x708eu, 0x5bffu, 0x99acu}) {
        render(face, ch);
        const auto original = bitmap(face);
        const int ascent = driver.GetAscent();
        const int bitmapAscent = driver.bitmapBaseline();
        const int rectAscent = driver.rectBaseline();
        const int top = 3 + bitmapAscent - original.top;
        const int bottom = top + static_cast<int>(original.rows);
        std::cout << label << " char=" << std::hex << ch << std::dec
                  << " baseline=" << ascent << "/" << bitmapAscent << "/" << rectAscent
                  << " rawY=" << top << "/" << bottom << " clip=0/126"
                  << " bottomCut=" << std::max(0, bottom - 126) << std::endl;
        require(ascent == bitmapAscent && ascent == rectAscent,
                "reported, drawn and measured glyph baselines disagree");
        require(top >= 3 && bottom <= 123,
                "font-declared EM box is shifted outside the actual 120+6 name field");
        require(original == bitmap(face) && face->size->metrics.y_ppem == 120,
                "baseline selection rescaled or changed glyph pixels/advance");
    }
    int line = 0, thickness = 0;
    driver.GetUnderline(line, thickness);
    const int originalAscent = face->ascender * 120 / face->units_per_EM;
    const int originalUnderline = (face->ascender - face->underline_position) * 120 / face->units_per_EM;
    require(std::abs(originalUnderline - line - (originalAscent - driver.GetAscent())) <= 1,
            "underline and glyph do not use the same baseline");
    driver.GetStrikeOut(line, thickness);
    require(std::abs(line - driver.GetAscent() * 7 / 10) <= 1,
            "strikeout and glyph do not use the same baseline");
    FT_Done_Face(face);
}

static void checkBodyLine(FT_Library library, const char *path) {
    FT_Face face = open(library, path, 40);
    ProductionFace driver(face);
    // Actual caller: a 40px font at y14, 3px outline, 58px line image.
    // Include CJK punctuation and Latin descenders as well as the body text.
    for(unsigned ch : {0x6211u, 0x662fu, 0x5dddu, 0x6751u, 0x67abu, 0x592au,
                       0xff0cu, 0x5927u, 0x5b66u, 0x751fu, 0x3002u, 0x41u, 0x67u}) {
        render(face, ch);
        const auto original = bitmap(face);
        const int top = 14 + driver.bitmapBaseline() - original.top;
        const int bottom = top + static_cast<int>(original.rows);
        std::cout << "bundled body char=" << std::hex << ch << std::dec
                  << " baseline=" << driver.GetAscent() << " rawY=" << top << '/' << bottom
                  << " outlineY=" << top - 3 << '/' << bottom + 3 << " clip=0/58\n";
        require(driver.GetAscent() == driver.bitmapBaseline() &&
                    driver.GetAscent() == driver.rectBaseline(),
                "body reported, drawn and measured baselines disagree");
        require(top - 3 >= 0 && bottom + 3 <= 58,
                "bundled CJK font clips body pixels or outline in the actual line image");
        require(original == bitmap(face) && face->size->metrics.y_ppem == 40,
                "body baseline selection rescaled glyph pixels or advance");
    }
    FT_Done_Face(face);
}

static void checkHintedIdeographTop(FT_Library library, const char *path) {
    FT_Face face = open(library, path, 44);
    ProductionFace driver(face);
    for(unsigned ch : {0x5168u, 0x5b66u, 0x6d3eu, 0x5bf9u, 0x662fu}) {
        render(face, ch);
        const auto original = bitmap(face);
        unsigned outsideInk = 0;
        for(unsigned y = 0; y < face->glyph->bitmap.rows; ++y)
            if(driver.bitmapBaseline() - face->glyph->bitmap_top + int(y) < 0)
                for(unsigned x = 0; x < face->glyph->bitmap.width; ++x)
                    outsideInk += face->glyph->bitmap.buffer[y * face->glyph->bitmap.pitch + x] != 0;
        std::cout << "hinted ideograph char=" << std::hex << ch << std::dec
                  << " baseline=" << driver.GetAscent() << " top=" << face->glyph->bitmap_top
                  << " excludedTopInk=" << outsideInk << std::endl;
        require(!outsideInk, "fractional font ascent truncation clips hinted ideograph ink at y0");
        require(driver.GetAscent() == driver.bitmapBaseline() && driver.GetAscent() == driver.rectBaseline(),
                "hinted ideograph baselines disagree");
        require(original == bitmap(face), "rounding changed glyph pixels or advance");
    }
    FT_Done_Face(face);
}

int main(int argc, char **argv) {
    try {
        require(argc == 7, "fixture font arguments missing");
        FT_Library library = nullptr;
        require(!FT_Init_FreeType(&library), "FreeType unavailable");
        checkHintedIdeographTop(library, argv[6]);
        checkBodyLine(library, argv[2]);
        if(*argv[4]) checkNameField(library, argv[4], "actual Heavy");
        // Real bundled glyph data plus explicit, valid font-declared EM metrics.
        checkNameField(library, argv[1], "declared fixture");
        const auto fixtureDirectory = std::filesystem::path(argv[1]).parent_path();
        {
            FT_Face face = open(library, argv[2], 120);
            ProductionFace driver(face);
            require(driver.GetAscent() == 92 && driver.bitmapBaseline() == 92 &&
                        driver.rectBaseline() == 92,
                    "CJK font without BASE did not use its declared OS/2 EM-box");
            FT_Done_Face(face);
        }
        for(const auto &path : {fixtureDirectory / "invalid-offset.ttf",
                 fixtureDirectory / "unsupported-coordinate.ttf",
                 fixtureDirectory / "oversized-base.ttf"}) {
            FT_Face face = open(library, path.c_str(), 120);
            ProductionFace driver(face);
            require(driver.GetAscent() == 126 && driver.bitmapBaseline() == 126 &&
                        driver.rectBaseline() == 126,
                    "malformed or unsupported explicit BASE was treated as absence");
            FT_Done_Face(face);
        }
        {
            FT_Face face = open(library, argv[2], 120);
            auto *metrics = static_cast<TT_OS2 *>(FT_Get_Sfnt_Table(face, ft_sfnt_os2));
            require(metrics != nullptr, "bundled font OS/2 metrics missing");
            const TT_OS2 original = *metrics;
            metrics->ulUnicodeRange1 = metrics->ulUnicodeRange2 = 0;
            metrics->ulCodePageRange1 = 0;
            require(ProductionFace(face).GetAscent() == 126,
                    "non-CJK font incorrectly acquired ideographic metrics");
            metrics->ulUnicodeRange2 = 1u << 16; // CJK punctuation alone
            require(ProductionFace(face).GetAscent() == 126,
                    "punctuation-only font incorrectly classified as CJK");
            metrics->ulUnicodeRange2 = 0;
            metrics->ulCodePageRange1 = 1u << 17; // JIS, no Unicode range declaration
            require(ProductionFace(face).GetAscent() == 92,
                    "CJK code-page declaration was ignored");
            metrics->version = 0; // code-page fields were introduced in version 1
            require(ProductionFace(face).GetAscent() == 126,
                    "OS/2 version 0 used unavailable code-page fields");
            *metrics = original;
            metrics->sTypoAscender = 0;
            require(ProductionFace(face).GetAscent() == 126,
                    "invalid typographic ascent changed the baseline");
            *metrics = original;
            metrics->sTypoDescender = 1;
            require(ProductionFace(face).GetAscent() == 126,
                    "invalid typographic descent changed the baseline");
            *metrics = original;
            metrics->version = 0xffff;
            require(ProductionFace(face).GetAscent() == 126,
                    "unavailable OS/2 metrics changed the baseline");
            *metrics = original;
            FT_Done_Face(face);
        }
        if(*argv[5]) {
            FT_Face face = open(library, argv[5], 30);
            ProductionFace driver(face);
            require(driver.GetAscent() == 27 && driver.bitmapBaseline() == 27 &&
                        driver.rectBaseline() == 27,
                    "actual body font rounded baseline disagrees with declared metrics");
            for(unsigned ch : {0x67d0u, 0x3001u, 0x9c81u}) {
                render(face, ch);
                const int top = 24 + driver.bitmapBaseline() - face->glyph->bitmap_top;
                const int bottom = top + face->glyph->bitmap.rows;
                require(top - 2 >= 0 && bottom + 2 <= 58,
                        "actual body glyph/outline exceeds the observed line image");
            }
            FT_Done_Face(face);
        }
#ifdef HAS_FONT_BASELINE_HELPER
        std::ifstream stream(argv[3], std::ios::binary);
        const std::vector<unsigned char> table((std::istreambuf_iterator<char>(stream)), {});
        require(FontBaseline::parseEmBoxAscent(table.data(), table.size(), 256) == 230,
                "explicit font EM-box coordinates not honored");
        bool absent = true;
        require(FontBaseline::parseEmBoxAscent(table.data(), table.size(), 256, &absent) == 230 && !absent,
                "explicit EM-box was classified as absent");
        auto noIdeographic = table;
        noIdeographic[14] = 'r'; noIdeographic[15] = 'o';
        noIdeographic[16] = 'm'; noIdeographic[17] = 'n';
        require(!FontBaseline::parseEmBoxAscent(noIdeographic.data(), noIdeographic.size(), 256, &absent) && absent,
                "valid BASE without horizontal ideo did not permit CJK fallback");
        auto unsupported = table;
        unsupported[unsupported.size()-4] = 0;
        unsupported[unsupported.size()-3] = 3;
        require(!FontBaseline::parseEmBoxAscent(unsupported.data(), unsupported.size(), 256, &absent) && !absent,
                "unsupported explicit coordinate was classified as absent");
        for(std::size_t n = 0; n < table.size(); ++n)
            require(!FontBaseline::parseEmBoxAscent(table.data(), n, 256),
                    "truncated font BASE data was accepted");
        auto broken = table;
        broken[4] = broken[5] = 0xff;
        require(!FontBaseline::parseEmBoxAscent(broken.data(), broken.size(), 256),
                "out-of-range axis offset was accepted");
        broken = table;
        broken[broken.size()-4] = 0;
        broken[broken.size()-3] = 3;
        require(!FontBaseline::parseEmBoxAscent(broken.data(), broken.size(), 256),
                "unsupported device/variation coordinate was accepted");
        require(!FontBaseline::parseEmBoxAscent(table.data(), table.size(), 0),
                "zero unitsPerEm was accepted");
        require(!FontBaseline::parseEmBoxAscent(table.data(), table.size(), 1000),
                "non-square declared EM box was accepted");
        require(!FontBaseline::parseEmBoxAscent(nullptr, table.size(), 256),
                "null table bytes were accepted");
        broken = table;
        broken[24] = 'X'; // missing DFLT script
        require(!FontBaseline::parseEmBoxAscent(broken.data(), broken.size(), 256),
                "missing default font script changed the baseline");
        broken = table;
        broken[37] = 1; // defaultIndex=idtp instead of ideo
        require(!FontBaseline::parseEmBoxAscent(broken.data(), broken.size(), 256),
                "non-ideographic default script changed the baseline");
        broken = table;
        broken[22] = broken[23] = 0xff; // invalid script count
        require(!FontBaseline::parseEmBoxAscent(broken.data(), broken.size(), 256),
                "oversized script count was accepted");
        broken = table;
        broken[20] = 'o'; broken[21] = 'n'; // idtp -> idon: no explicit top
        require(FontBaseline::parseEmBoxAscent(broken.data(), broken.size(), 256) == 230,
                "ideo plus unitsPerEm did not derive the declared box top");
        broken = table;
        broken[3] = 1;
        broken.insert(broken.begin() + 8, 4, 0);
        broken[5] = 12;
        require(FontBaseline::parseEmBoxAscent(broken.data(), broken.size(), 256) == 230,
                "non-variable BASE version 1.1 was rejected");
        broken[11] = 1;
        require(!FontBaseline::parseEmBoxAscent(broken.data(), broken.size(), 256),
                "unsupported variation store was accepted");
#endif
        FT_Done_FreeType(library);
        std::cout << "font baseline: real glyph clipping, shared metrics, CJK fallback and invalid tables passed\n";
        return 0;
    } catch(const std::exception &error) {
        std::cerr << "font baseline: " << error.what() << '\n';
        return 1;
    }
}
