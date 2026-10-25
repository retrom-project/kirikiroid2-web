#ifndef TVP_FONT_BASELINE_H
#define TVP_FONT_BASELINE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

namespace FontBaseline {

constexpr std::size_t maxTableBytes = 64 * 1024;

namespace detail {
struct Table {
    const unsigned char *bytes;
    std::size_t size;

    bool contains(std::size_t at, std::size_t length) const {
        return at <= size && length <= size - at;
    }
    std::optional<unsigned> word(std::size_t at) const {
        if(!contains(at, 2)) return {};
        return (unsigned(bytes[at]) << 8) | bytes[at + 1];
    }
    std::optional<std::uint32_t> tag(std::size_t at) const {
        const auto first = word(at), second = word(at + 2);
        if(!first || !second) return {};
        return (std::uint32_t(*first) << 16) | *second;
    }
};
} // namespace detail

// OpenType BASE/Baseline Tags: ideo is the EM-box bottom; if idtp is
// absent, its top is ideo + unitsPerEm. Accept only a font's explicit
// default ideographic box, without guessing script metrics from glyph ink.
inline std::optional<int> parseEmBoxAscent(const unsigned char *data,
                                         std::size_t length,
                                         unsigned unitsPerEm,
                                         bool *absentIdeographicBox = nullptr) {
    if(absentIdeographicBox) *absentIdeographicBox = false;
    if(!data || length < 8 || length > maxTableBytes || !unitsPerEm || unitsPerEm > 0xffff)
        return {};
    const detail::Table table{data, length};
    const auto major = table.word(0), minor = table.word(2);
    if(!major || !minor || *major != 1 || *minor > 1) return {};
    if(*minor == 1) {
        const auto variations = table.tag(8);
        if(!variations || *variations) return {};
    }
    const auto axis = table.word(4);
    if(!axis) return {};
    if(!*axis) {
        if(absentIdeographicBox) *absentIdeographicBox = true;
        return {};
    }
    const auto tagOffset = table.word(*axis), scriptOffset = table.word(*axis + 2);
    if(!tagOffset || !scriptOffset) return {};
    if(!*tagOffset) {
        if(absentIdeographicBox) *absentIdeographicBox = true;
        return {};
    }
    const std::size_t tags = *axis + *tagOffset;
    const std::size_t scripts = *axis + *scriptOffset;
    const auto tagCount = table.word(tags), scriptCount = table.word(scripts);
    if(!tagCount || *tagCount > 64 || !table.contains(tags + 2, *tagCount * 4)) return {};

    std::optional<unsigned> bottomIndex, topIndex;
    for(unsigned i = 0; i < *tagCount; ++i) {
        const auto tag = *table.tag(tags + 2 + i * 4);
        if(tag == FT_MAKE_TAG('i', 'd', 'e', 'o')) {
            if(bottomIndex) return {};
            bottomIndex = i;
        } else if(tag == FT_MAKE_TAG('i', 'd', 't', 'p')) {
            if(topIndex) return {};
            topIndex = i;
        }
    }
    if(!bottomIndex) {
        if(absentIdeographicBox) *absentIdeographicBox = true;
        return {};
    }
    if(!*scriptOffset || !scriptCount || !*scriptCount || *scriptCount > 256 ||
       !table.contains(scripts + 2, *scriptCount * 6)) return {};

    std::optional<std::size_t> defaultScript;
    for(unsigned i = 0; i < *scriptCount; ++i) {
        const auto record = scripts + 2 + i * 6;
        if(*table.tag(record) == FT_MAKE_TAG('D', 'F', 'L', 'T')) {
            if(defaultScript) return {};
            const auto offset = *table.word(record + 4);
            if(!offset) return {};
            defaultScript = scripts + offset;
        }
    }
    if(!defaultScript) return {};
    const auto valuesOffset = table.word(*defaultScript);
    if(!valuesOffset || !*valuesOffset) return {};
    const std::size_t values = *defaultScript + *valuesOffset;
    const auto defaultIndex = table.word(values), count = table.word(values + 2);
    if(!defaultIndex || *defaultIndex != *bottomIndex || !count || *count != *tagCount ||
       !table.contains(values + 4, *count * 2)) return {};

    const auto coordinate = [&](unsigned index) -> std::optional<int> {
        const auto offset = *table.word(values + 4 + index * 2);
        if(!offset) return {};
        const auto at = values + offset;
        const auto format = table.word(at), value = table.word(at + 2);
        // Contour-point/device/variation adjustments require additional layout
        // machinery. Keep the existing baseline when they are not supported.
        if(!format || *format != 1 || !value) return {};
        return static_cast<std::int16_t>(*value);
    };
    const auto bottom = coordinate(*bottomIndex);
    if(!bottom) return {};
    const auto top = topIndex ? coordinate(*topIndex)
                             : std::optional<int>(*bottom + int(unitsPerEm));
    if(!top || *top <= 0 || *bottom > 0 || *top - *bottom != int(unitsPerEm))
        return {};
    return top;
}

inline std::optional<int> cjkTypographicAscent(FT_Face face) {
    const auto *metrics = static_cast<const TT_OS2 *>(FT_Get_Sfnt_Table(face, ft_sfnt_os2));
    if(!metrics || metrics->version == 0xffff) return {};
    // OS/2 Unicode ranges: Hangul Jamo; Hiragana, Katakana, Bopomofo,
    // Hangul compatibility Jamo/syllables, and CJK ideographs. Punctuation
    // alone does not establish that a font is designed for CJK text.
    constexpr std::uint32_t cjkRanges2 = (1u << 17) | (1u << 18) | (1u << 19) |
        (1u << 20) | (1u << 24) | (1u << 27) | (1u << 29);
    constexpr std::uint32_t cjkCodePages = 0x003e0000u; // JIS, GB, Wansung, Big5, Johab
    const bool cjk = (metrics->ulUnicodeRange1 & (1u << 28)) ||
        (metrics->ulUnicodeRange2 & cjkRanges2) ||
        (metrics->version >= 1 && (metrics->ulCodePageRange1 & cjkCodePages));
    if(!cjk || metrics->sTypoAscender <= 0 || metrics->sTypoDescender > 0)
        return {};
    return metrics->sTypoAscender;
}

inline int designAscent(FT_Face face) {
    const int existing = face->ascender;
#ifdef __EMSCRIPTEN__
    if(!FT_IS_SCALABLE(face) || !FT_IS_SFNT(face) || FT_HAS_MULTIPLE_MASTERS(face))
        return existing;
    FT_ULong length = 0;
    constexpr FT_ULong tag = FT_MAKE_TAG('B', 'A', 'S', 'E');
    const auto status = FT_Load_Sfnt_Table(face, tag, 0, nullptr, &length);
    bool absent = status == FT_Err_Table_Missing;
    if(!status && length >= 8 && length <= maxTableBytes) {
        std::vector<unsigned char> bytes(length);
        if(!FT_Load_Sfnt_Table(face, tag, 0, bytes.data(), &length) &&
           length == bytes.size()) {
            const auto ascent = parseEmBoxAscent(bytes.data(), bytes.size(), face->units_per_EM, &absent);
            if(ascent) return *ascent;
        }
    }
    // OpenType Baseline Tags / Ideographic em-box: when no usable horizontal
    // ideo baseline is declared, a CJK font's OS/2 sTypoAscender/Descender
    // define its EM-box. hhea/win metrics may instead include tall glyphs
    // outside that box. Keep non-CJK and unavailable metrics unchanged.
    // Malformed or unsupported explicit declarations are not absence.
    return absent ? cjkTypographicAscent(face).value_or(existing) : existing;
#else
    return existing;
#endif
}

} // namespace FontBaseline

#endif
