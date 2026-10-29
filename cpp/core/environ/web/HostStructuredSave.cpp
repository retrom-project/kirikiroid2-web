#include "tjsCommHead.h"
#include "HostStructuredSave.h"
#include "StorageIntf.h"

#include <cstdint>
#include <memory>

namespace {
std::uint32_t little32(const unsigned char *p) {
    return p[0] | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
unsigned little16(const unsigned char *p) { return p[0] | (unsigned(p[1]) << 8); }
}

bool TVPIsImageBackedStructuredSave(const ttstr &name, const ttstr &mode) noexcept {
    // A positive native `o` offset appends structured data after a thumbnail.
    // Settings-only saveStruct calls have no such complete image prefix.
    try {
        const auto *offset_mode = TJS_strchr(mode.c_str(), TJS_W('o'));
        if(name.IsEmpty() || !offset_mode ||
           TJS_strchr(offset_mode + 1, TJS_W('o'))) return false;
        std::uint64_t offset = 0;
        const auto *digit = offset_mode + 1;
        if(*digit < TJS_W('0') || *digit > TJS_W('9')) return false;
        while(*digit >= TJS_W('0') && *digit <= TJS_W('9')) {
            offset = offset * 10 + (*digit++ - TJS_W('0'));
            if(offset > UINT32_MAX) return false;
        }
        if(offset < 54) return false;
        std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(name, TJS_BS_READ));
        unsigned char header[54];
        if(stream->GetSize() != offset || stream->Read(header, sizeof(header)) != sizeof(header) ||
           header[0] != 'B' || header[1] != 'M' || little32(header + 2) != offset ||
           little32(header + 6) != 0 || little32(header + 10) != 54 ||
           little32(header + 14) != 40 || little16(header + 26) != 1 ||
           little32(header + 30) != 0) return false;
        const auto width = static_cast<std::int32_t>(little32(header + 18));
        const auto height = static_cast<std::int32_t>(little32(header + 22));
        const auto bits = little16(header + 28);
        if(width <= 0 || height == 0 || (bits != 24 && bits != 32)) return false;
        const std::uint64_t rows = height < 0 ? -std::int64_t(height) : height;
        const std::uint64_t stride = ((std::uint64_t(width) * bits + 31) / 32) * 4;
        const std::uint64_t pixels = rows * stride;
        const auto image_size = little32(header + 34);
        return pixels + 54 == offset && (image_size == 0 || image_size == pixels);
    } catch(...) {
        // Observation cannot change the original native write or exception.
        return false;
    }
}
