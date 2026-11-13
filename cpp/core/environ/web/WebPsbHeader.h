#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace TVPWebPsbHeaderDetail {
    inline std::uint32_t read32(const std::uint8_t *bytes) {
        return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8u) |
            (std::uint32_t(bytes[2]) << 16u) |
            (std::uint32_t(bytes[3]) << 24u);
    }

    inline std::uint32_t checksum(const std::uint8_t *header,
                                  std::size_t length) {
        std::uint32_t a = 1, b = 0;
        for(std::size_t at = 8; at < length; ++at) {
            if(at >= 40 && at < 44) continue;
            a += header[at];
            b += a;
        }
        return ((b % 65521u) << 16u) | (a % 65521u);
    }

    inline bool valid(const std::uint8_t *header, std::size_t length,
                      std::size_t size) {
        if(read32(header + 8) != length ||
           read32(header + 40) != checksum(header, length)) return false;
        for(std::size_t at = 12; at < 40; at += 4) {
            const auto offset = read32(header + at);
            if(offset < length || offset > size) return false;
        }
        const auto names = read32(header + 12);
        const auto entries = read32(header + 36);
        const auto strings = read32(header + 16);
        if(names >= size || entries >= size || names > entries ||
           entries > strings || strings > read32(header + 20) ||
           read32(header + 24) > read32(header + 28) ||
           read32(header + 28) > read32(header + 32)) return false;
        for(std::size_t at = 44; at < length; at += 4) {
            if(read32(header + at) > size) return false;
        }
        return true;
    }
}

// Web cannot load the PC Emote DLL which normally decodes these headers.
// This bounded storage adaptation precedes the native PSB owner/view lifetime.
// Explicit native decrypt filters bypass it at the caller.
inline bool TVPWebDecodePsbHeader(std::uint8_t *data, std::size_t size) {
    using namespace TVPWebPsbHeaderDetail;
    if(size < 8 || read32(data) != 0x00425350u ||
       (data[6] == 0 && data[7] == 0)) return true;
    const unsigned version = unsigned(data[4]) | (unsigned(data[5]) << 8u);
    if(data[6] != 1 || data[7] != 0 || (version != 3 && version != 4))
        return false;
    const std::size_t length = version == 4 ? 56 : 44;
    if(size < length) return false;
    const auto first = read32(data + 8) ^ std::uint32_t(length);
    std::array<std::uint8_t, 56> accepted{};
    bool found = false;
    // The native stream refills when the remaining word becomes zero, so its
    // first word can occupy one through four bytes. The header checksum and
    // offsets validate the result before any input byte is changed.
    for(unsigned width = 1; width <= 4; ++width) {
        const auto mask = width == 4 ? UINT32_MAX :
            (std::uint32_t(1) << (width * 8u)) - 1u;
        const auto word = first & mask;
        if(width > 1 && (word >> ((width - 1u) * 8u)) == 0) continue;
        const std::uint32_t t0 = 123456789u ^ (123456789u << 11u);
        const auto inverse = word ^ t0 ^ (t0 >> 8u);
        std::uint32_t x = 123456789u, y = 362436069u, z = 521288629u;
        std::uint32_t w = inverse ^ (inverse >> 19u), remaining = 0;
        std::array<std::uint8_t, 56> candidate{};
        std::copy_n(data, length, candidate.data());
        for(std::size_t at = 8; at < length; ++at) {
            if(remaining == 0) {
                const auto t = x ^ (x << 11u);
                x = y; y = z; z = w;
                w = w ^ (w >> 19u) ^ t ^ (t >> 8u);
                remaining = w;
            }
            candidate[at] ^= std::uint8_t(remaining);
            remaining >>= 8u;
        }
        if(!valid(candidate.data(), length, size)) continue;
        if(found && !std::equal(candidate.begin(), candidate.begin() + length,
                                accepted.begin())) return false;
        accepted = candidate;
        found = true;
    }
    if(!found) return false;
    std::memcpy(data + 8, accepted.data() + 8, length - 8);
    data[6] = data[7] = 0;
    return true;
}
