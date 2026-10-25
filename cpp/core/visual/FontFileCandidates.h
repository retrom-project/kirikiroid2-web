#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace FontFileCandidates {

inline bool filename(std::u16string_view name) {
    if(name.size() < 4) return false;
    auto extension = name.substr(name.size() - 4);
    if(extension[0] != u'.') return false;
    const auto lower = [](char16_t value) { return value >= u'A' && value <= u'Z' ? value + (u'a' - u'A') : value; };
    const auto a = lower(extension[1]), b = lower(extension[2]), c = lower(extension[3]);
    return (a == u't' && b == u't' && (c == u'f' || c == u'c')) ||
           (a == u'o' && b == u't' && (c == u'f' || c == u'c'));
}

inline bool header(const unsigned char *bytes) {
    return (bytes[0] == 0 && bytes[1] == 1 && bytes[2] == 0 && bytes[3] == 0) ||
        (bytes[0] == 'O' && bytes[1] == 'T' && bytes[2] == 'T' && bytes[3] == 'O') ||
        (bytes[0] == 't' && bytes[1] == 't' && bytes[2] == 'c' && bytes[3] == 'f') ||
        (bytes[0] == 't' && bytes[1] == 'r' && bytes[2] == 'u' && bytes[3] == 'e');
}

struct Budget {
    static constexpr std::size_t DirectoryEntries = 4096;
    static constexpr unsigned Files = 64;
    static constexpr std::uint64_t FileBytes = 64 * 1024 * 1024;
    static constexpr std::uint64_t TotalBytes = 128 * 1024 * 1024;
    unsigned files = 0;
    std::uint64_t bytes = 0;
    bool take(std::uint64_t size) {
        if(size < 4 || size > FileBytes || files >= Files || size > TotalBytes - bytes) return false;
        ++files; bytes += size; return true;
    }
};

} // namespace FontFileCandidates
