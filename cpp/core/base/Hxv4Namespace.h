// Web-only normalized container representation; see
// analysis/web-hxv4-namespace-four-reference.md. No content-decryption key or
// game-specific plaintext-name dictionary is part of this representation.
#ifndef TVPWebHxv4NamespaceH
#define TVPWebHxv4NamespaceH

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

class tTVPWebHxv4Namespace {
public:
    using tPathHash = std::array<tjs_uint8, 8>;
    using tFileHash = std::array<tjs_uint8, 32>;
    using tKey = std::array<tjs_uint8, 40>;

private:
    struct tBinding {
        tKey Key;
        tjs_uint32 Id;
        tjs_uint16 Slot;
        tjs_uint16 Flags;
        tjs_uint32 FileIndex;
        ttstr PhysicalName;
    };

    bool Loaded = false;
    ttstr MediaName;
    std::vector<tBinding> Entries;
    std::map<tKey, ttstr> Lookup;

    [[noreturn]] static void Invalid() {
        throw std::runtime_error("Invalid RHx4 hash namespace");
    }

    static tjs_uint32 Read32(const tjs_uint8 *p) {
        return tjs_uint32(p[0]) | (tjs_uint32(p[1]) << 8) |
            (tjs_uint32(p[2]) << 16) | (tjs_uint32(p[3]) << 24);
    }

    static tjs_uint64 Read64(const tjs_uint8 *p) {
        return Read32(p) | (tjs_uint64(Read32(p + 4)) << 32);
    }

    static void Write32(tjs_uint8 *p, tjs_uint32 x) {
        for(unsigned i = 0; i < 4; ++i) p[i] = x >> (8 * i);
    }

    static std::vector<tjs_uint8> Utf16Bytes(const ttstr &value) {
        std::vector<tjs_uint8> bytes;
        bytes.reserve(size_t(value.GetLen()) * 2);
        auto append = [&bytes](tjs_uint32 unit) {
            bytes.push_back(unit & 255);
            bytes.push_back((unit >> 8) & 255);
        };
        for(tjs_int i = 0; i < value.GetLen(); ++i) {
            tjs_uint32 unit = value[i];
            if(unit > 0x10ffff) Invalid();
            if(unit > 0xffff) {
                unit -= 0x10000;
                append(0xd800 | (unit >> 10));
                append(0xdc00 | (unit & 1023));
            } else {
                append(unit);
            }
        }
        return bytes;
    }

    static tjs_uint64 Rot64(tjs_uint64 x, unsigned n) {
        return (x << n) | (x >> (64 - n));
    }

    static tPathHash SipHash(const std::vector<tjs_uint8> &bytes) {
        tjs_uint64 v0 = 0x736f6d6570736575ULL;
        tjs_uint64 v1 = 0x646f72616e646f6dULL;
        tjs_uint64 v2 = 0x6c7967656e657261ULL;
        tjs_uint64 v3 = 0x7465646279746573ULL;
        auto round = [&]() {
            v0 += v1; v1 = Rot64(v1, 13); v1 ^= v0; v0 = Rot64(v0, 32);
            v2 += v3; v3 = Rot64(v3, 16); v3 ^= v2;
            v0 += v3; v3 = Rot64(v3, 21); v3 ^= v0;
            v2 += v1; v1 = Rot64(v1, 17); v1 ^= v2; v2 = Rot64(v2, 32);
        };
        size_t at = 0;
        for(; bytes.size() - at >= 8; at += 8) {
            const auto block = Read64(bytes.data() + at);
            v3 ^= block; round(); round(); v0 ^= block;
        }
        tjs_uint64 last = tjs_uint64(bytes.size() & 255) << 56;
        for(size_t i = at; i < bytes.size(); ++i)
            last |= tjs_uint64(bytes[i]) << (8 * (i - at));
        v3 ^= last; round(); round(); v0 ^= last; v2 ^= 255;
        round(); round(); round(); round();
        tPathHash result{};
        const auto hash = v0 ^ v1 ^ v2 ^ v3;
        for(unsigned i = 0; i < 8; ++i) result[i] = hash >> (8 * i);
        return result;
    }

    static tjs_uint32 Rot32(tjs_uint32 x, unsigned n) {
        return (x >> n) | (x << (32 - n));
    }

    static tFileHash Blake2s(const std::vector<tjs_uint8> &bytes) {
        static constexpr tjs_uint32 iv[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };
        static constexpr tjs_uint8 sigma[10][16] = {
            {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
            {14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3},
            {11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4},
            {7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8},
            {9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13},
            {2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9},
            {12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11},
            {13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10},
            {6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5},
            {10,2,8,4,7,6,1,5,15,11,9,14,3,12,13,0}
        };
        tjs_uint32 h[8];
        std::copy(iv, iv + 8, h);
        h[0] ^= 0x01010020;
        tjs_uint64 total = 0;
        auto compress = [&](const tjs_uint8 *block, size_t size, bool final) {
            tjs_uint8 padded[64]{};
            std::copy(block, block + size, padded);
            tjs_uint32 m[16], v[16];
            for(unsigned i = 0; i < 16; ++i) m[i] = Read32(padded + i * 4);
            std::copy(h, h + 8, v);
            std::copy(iv, iv + 8, v + 8);
            total += size;
            v[12] ^= tjs_uint32(total);
            v[13] ^= tjs_uint32(total >> 32);
            if(final) v[14] = ~v[14];
            auto mix = [&](unsigned a, unsigned b, unsigned c, unsigned d,
                           tjs_uint32 x, tjs_uint32 y) {
                v[a] += v[b] + x; v[d] = Rot32(v[d] ^ v[a], 16);
                v[c] += v[d]; v[b] = Rot32(v[b] ^ v[c], 12);
                v[a] += v[b] + y; v[d] = Rot32(v[d] ^ v[a], 8);
                v[c] += v[d]; v[b] = Rot32(v[b] ^ v[c], 7);
            };
            for(const auto &s : sigma) {
                mix(0,4,8,12,m[s[0]],m[s[1]]); mix(1,5,9,13,m[s[2]],m[s[3]]);
                mix(2,6,10,14,m[s[4]],m[s[5]]); mix(3,7,11,15,m[s[6]],m[s[7]]);
                mix(0,5,10,15,m[s[8]],m[s[9]]); mix(1,6,11,12,m[s[10]],m[s[11]]);
                mix(2,7,8,13,m[s[12]],m[s[13]]); mix(3,4,9,14,m[s[14]],m[s[15]]);
            }
            for(unsigned i = 0; i < 8; ++i) h[i] ^= v[i] ^ v[i + 8];
        };
        size_t at = 0;
        while(bytes.size() - at > 64) {
            compress(bytes.data() + at, 64, false);
            at += 64;
        }
        // MediaName is nonempty, so the hash input always has storage.
        compress(bytes.data() + at, bytes.size() - at, true);
        tFileHash result{};
        for(unsigned i = 0; i < 8; ++i) Write32(result.data() + i * 4, h[i]);
        return result;
    }

    void Parse(const tjs_uint8 *data, size_t size) {
        if(Loaded || size < 8) Invalid();
        const auto units = Read32(data);
        if(units < 1 || units > 256 || size < 8 + units * 2) Invalid();
        tjs_uint16 media[257]{};
        for(tjs_uint32 i = 0; i < units; ++i) {
            const auto unit = tjs_uint16(data[4 + i * 2]) |
                (tjs_uint16(data[5 + i * 2]) << 8);
            if(unit <= 32 || unit == 127 || unit == '/' || unit == '\\' ||
               unit == ':') Invalid();
            media[i] = unit;
        }
        for(tjs_uint32 i = 0; i < units; ++i) {
            if(media[i] >= 0xd800 && media[i] <= 0xdbff) {
                if(++i >= units || media[i] < 0xdc00 || media[i] > 0xdfff) Invalid();
            } else if(media[i] >= 0xdc00 && media[i] <= 0xdfff) {
                Invalid();
            }
        }
        MediaName = TVPStringFromBMPUnicode(media, units);
        const auto count = Read32(data + 4 + units * 2);
        if(count < 1 || count > 250000 || size - 8 - units * 2 != size_t(count) * 52)
            Invalid();
        std::set<tKey> keys;
        std::set<tjs_uint32> ids, indices;
        Entries.reserve(count);
        for(tjs_uint32 i = 0; i < count; ++i) {
            const auto *p = data + 8 + units * 2 + size_t(i) * 52;
            tBinding binding{};
            std::copy(p, p + 40, binding.Key.begin());
            binding.Id = Read32(p + 40);
            binding.Slot = p[44] | (tjs_uint16(p[45]) << 8);
            binding.Flags = p[46] | (tjs_uint16(p[47]) << 8);
            binding.FileIndex = Read32(p + 48);
            if(binding.Slot != 0 || !keys.insert(binding.Key).second ||
               !ids.insert(binding.Id).second || !indices.insert(binding.FileIndex).second)
                Invalid();
            Entries.push_back(binding);
        }
        Loaded = true;
    }

    static ttstr SyntheticName(tjs_uint32 id) {
        tjs_char units[4]{};
        unsigned count = 0;
        do {
            units[count++] = tjs_char(0x5000 + (id & 0x3fff));
            id >>= 14;
        } while(id);
        return ttstr(units, count);
    }

public:
    bool IsLoaded() const { return Loaded; }
    const ttstr &GetMediaName() const { return MediaName; }

    void ReadIndex(const tjs_uint8 *index, size_t size) {
        size_t at = 0;
        while(size - at >= 12) {
            const auto length = Read64(index + at + 4);
            const bool own = std::memcmp(index + at, "RHx4", 4) == 0;
            if(length > size - at - 12) {
                if(own || Loaded) Invalid();
                return; // Preserve the ordinary loader's unrelated format path.
            }
            if(own) Parse(index + at + 12, size_t(length));
            at += 12 + size_t(length);
        }
        if(Loaded && at != size) Invalid();
    }

    template<typename TItems> void Bind(const TItems &items) {
        if(!Loaded) return;
        std::set<ttstr> names, bound;
        for(const auto &item : items) {
            if(!names.insert(item.Name).second) Invalid();
        }
        for(auto &binding : Entries) {
            if(binding.FileIndex >= items.size()) Invalid();
            binding.PhysicalName = items[binding.FileIndex].Name;
            if(binding.PhysicalName != SyntheticName(binding.Id)) Invalid();
            bound.insert(binding.PhysicalName);
            Lookup.emplace(binding.Key, binding.PhysicalName);
        }
        for(const auto &item : items) {
            bool synthetic = item.Name.GetLen() >= 1 && item.Name.GetLen() <= 3;
            for(tjs_int i = 0; synthetic && i < item.Name.GetLen(); ++i)
                synthetic = item.Name[i] >= 0x5000 && item.Name[i] <= 0x8fff;
            if(synthetic && !bound.count(item.Name)) Invalid();
        }
    }

    tPathHash PathHash(const ttstr &directory) const {
        return SipHash(Utf16Bytes(directory + MediaName));
    }

    tFileHash FileHash(const ttstr &name) const {
        return Blake2s(Utf16Bytes(name + MediaName));
    }

    bool Resolve(const ttstr &directory, const ttstr &basename, ttstr &physical) const {
        tKey key{};
        const auto path = PathHash(directory);
        const auto file = FileHash(basename);
        std::copy(path.begin(), path.end(), key.begin());
        std::copy(file.begin(), file.end(), key.begin() + 8);
        const auto found = Lookup.find(key);
        if(found == Lookup.end()) return false;
        physical = found->second;
        return true;
    }

    std::map<tFileHash, ttstr> List(const ttstr &directory) const {
        const auto path = PathHash(directory);
        std::map<tFileHash, ttstr> result;
        for(const auto &binding : Entries) {
            if(std::equal(path.begin(), path.end(), binding.Key.begin())) {
                tFileHash file{};
                std::copy(binding.Key.begin() + 8, binding.Key.end(), file.begin());
                result.emplace(file, binding.PhysicalName);
            }
        }
        return result;
    }
};
#endif
