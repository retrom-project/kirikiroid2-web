#include "Crypt.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include "xxhash/xxhash.h"

namespace DataPack {
namespace {

using Word = std::uint32_t;

Word LoadLE(const std::uint8_t *p) {
    return Word(p[0]) | (Word(p[1]) << 8) | (Word(p[2]) << 16) |
        (Word(p[3]) << 24);
}

void StoreLE(std::uint8_t *p, Word value) {
    for(unsigned i = 0; i < 4; ++i)
        p[i] = static_cast<std::uint8_t>(value >> (i * 8));
}

Word RotateLeft(Word value, unsigned count) {
    return (value << count) | (value >> (32 - count));
}

Word RotateRight(Word value, unsigned count) {
    return (value >> count) | (value << (32 - count));
}

constexpr Word BlakeIV[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

constexpr std::uint8_t BlakeSchedule[10][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
    { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
    { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
    { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
    { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
    { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
    { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
    { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
    { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 }
};

class Blake2s {
    std::array<Word, 8> hash;
    std::array<std::uint8_t, 64> buffer{};
    std::size_t used = 0;
    std::uint64_t count = 0;

    void Compress(bool final) {
        Word m[16];
        Word v[16];
        for(unsigned i = 0; i < 16; ++i)
            m[i] = LoadLE(buffer.data() + 4 * i);
        std::copy(hash.begin(), hash.end(), v);
        std::copy(std::begin(BlakeIV), std::end(BlakeIV), v + 8);
        v[12] ^= static_cast<Word>(count);
        v[13] ^= static_cast<Word>(count >> 32);
        if(final)
            v[14] = ~v[14];
        auto mix = [&](unsigned a, unsigned b, unsigned c, unsigned d,
                       Word x, Word y) {
            v[a] += v[b] + x;
            v[d] = RotateRight(v[d] ^ v[a], 16);
            v[c] += v[d];
            v[b] = RotateRight(v[b] ^ v[c], 12);
            v[a] += v[b] + y;
            v[d] = RotateRight(v[d] ^ v[a], 8);
            v[c] += v[d];
            v[b] = RotateRight(v[b] ^ v[c], 7);
        };
        for(const auto &s : BlakeSchedule) {
            mix(0, 4, 8, 12, m[s[0]], m[s[1]]);
            mix(1, 5, 9, 13, m[s[2]], m[s[3]]);
            mix(2, 6, 10, 14, m[s[4]], m[s[5]]);
            mix(3, 7, 11, 15, m[s[6]], m[s[7]]);
            mix(0, 5, 10, 15, m[s[8]], m[s[9]]);
            mix(1, 6, 11, 12, m[s[10]], m[s[11]]);
            mix(2, 7, 8, 13, m[s[12]], m[s[13]]);
            mix(3, 4, 9, 14, m[s[14]], m[s[15]]);
        }
        for(unsigned i = 0; i < 8; ++i)
            hash[i] ^= v[i] ^ v[i + 8];
    }

public:
    Blake2s() {
        std::copy(std::begin(BlakeIV), std::end(BlakeIV), hash.begin());
        // 32-byte digest, four-byte key, fanout=1, depth=1.
        hash[0] ^= 0x01010420;
    }

    void Update(const void *input, std::size_t size) {
        const auto *bytes = static_cast<const std::uint8_t *>(input);
        while(size) {
            if(used == buffer.size()) {
                count += buffer.size();
                Compress(false);
                used = 0;
            }
            const auto take = std::min(size, buffer.size() - used);
            std::memcpy(buffer.data() + used, bytes, take);
            used += take;
            bytes += take;
            size -= take;
        }
    }

    std::array<std::uint8_t, 32> Final() {
        count += used;
        std::fill(buffer.begin() + used, buffer.end(), 0);
        Compress(true);
        std::array<std::uint8_t, 32> digest;
        for(unsigned i = 0; i < hash.size(); ++i)
            StoreLE(digest.data() + 4 * i, hash[i]);
        return digest;
    }
};

std::array<std::uint8_t, 32> MakeKey(Word seed, const std::string &iv) {
    Blake2s hash;
    std::array<std::uint8_t, 64> keyBlock{};
    StoreLE(keyBlock.data(), seed);
    hash.Update(keyBlock.data(), keyBlock.size());
    hash.Update(iv.data(), iv.size());
    return hash.Final();
}

void ChaChaBlock(const std::array<Word, 16> &complemented,
                 unsigned rounds, Word *output) {
    Word words[16];
    for(unsigned i = 0; i < 16; ++i)
        words[i] = ~complemented[i];
    auto quarter = [&](unsigned a, unsigned b, unsigned c, unsigned d) {
        words[a] += words[b];
        words[d] = RotateLeft(words[d] ^ words[a], 16);
        words[c] += words[d];
        words[b] = RotateLeft(words[b] ^ words[c], 12);
        words[a] += words[b];
        words[d] = RotateLeft(words[d] ^ words[a], 8);
        words[c] += words[d];
        words[b] = RotateLeft(words[b] ^ words[c], 7);
    };
    for(unsigned round = 0; round < rounds; round += 2) {
        quarter(0, 4, 8, 12);
        quarter(1, 5, 9, 13);
        quarter(2, 6, 10, 14);
        quarter(3, 7, 11, 15);
        quarter(0, 5, 10, 15);
        quarter(1, 6, 11, 12);
        quarter(2, 7, 8, 13);
        quarter(3, 4, 9, 14);
    }
    for(unsigned i = 0; i < 16; ++i)
        output[i] = words[i] + ~complemented[i];
}

} // namespace

BasicCryptFilter::BasicCryptFilter(Word seed, const std::string &iv, int mode) {
    static constexpr unsigned modeRounds[] = { 8, 12, 20, 8, 12, 20 };
    static constexpr unsigned modeFactors[] = { 16, 8, 4, 1, 1, 1 };
    if(mode < 1 || mode > 6)
        throw std::runtime_error("unsupported data-pack crypt mode");
    rounds = modeRounds[mode - 1];
    factor = modeFactors[mode - 1];
    buffer.resize(64 * factor);
    position = buffer.size();
    const auto key = MakeKey(seed, iv);
    constexpr std::uint8_t constants[] = "expand 32-byte k";
    for(unsigned i = 0; i < 4; ++i)
        state[i] = ~LoadLE(constants + 4 * i);
    for(unsigned i = 0; i < 8; ++i)
        state[i + 4] = ~LoadLE(key.data() + 4 * i);
    const Word ivHash = XXH32(iv.data(), iv.size(), seed);
    state[12] = ~Word(0);
    state[13] = ~Word(0);
    state[14] = ~ivHash;
    state[15] = ~seed;
    fallback = ivHash != seed ? ivHash ^ seed : seed ? seed : ~Word(0);
}

void BasicCryptFilter::Fill() {
    state[12] = ~static_cast<Word>(counter);
    state[13] = ~static_cast<Word>(counter >> 32);
    ++counter;
    std::array<Word, 256> words;
    ChaChaBlock(state, rounds, words.data());
    for(unsigned i = 16; i < factor * 16; ++i) {
        Word value = words[i - 16];
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        words[i] = value ? value : fallback;
    }
    for(unsigned i = 0; i < factor * 16; ++i)
        StoreLE(buffer.data() + 4 * i, words[i]);
    position = 0;
}

void BasicCryptFilter::Transform(void *data, std::size_t size) {
    auto *bytes = static_cast<std::uint8_t *>(data);
    while(size) {
        if(position == buffer.size())
            Fill();
        const auto take = std::min(size, buffer.size() - position);
        for(std::size_t i = 0; i < take; ++i)
            bytes[i] ^= buffer[position + i];
        position += take;
        bytes += take;
        size -= take;
    }
}

} // namespace DataPack
