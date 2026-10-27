#ifndef KRKR2_DATAPACK_CRYPT_H
#define KRKR2_DATAPACK_CRYPT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace DataPack {

class BasicCryptFilter {
    std::array<std::uint32_t, 16> state;
    std::uint64_t counter = 0;
    unsigned rounds;
    unsigned factor;
    std::uint32_t fallback;
    std::vector<std::uint8_t> buffer;
    std::size_t position;

    void Fill();

public:
    BasicCryptFilter(std::uint32_t seed, const std::string &iv, int mode);
    void Transform(void *data, std::size_t size);
};

} // namespace DataPack

#endif
