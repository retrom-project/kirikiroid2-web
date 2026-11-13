#include "cpp/core/environ/web/WebPsbHeader.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char **argv) {
    assert(argc > 1);
    for(int file = 1; file < argc; ++file) {
        std::ifstream input(argv[file], std::ios::binary);
        assert(input);
        const std::vector<std::uint8_t> original(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        assert(original.size() >= 64);
        auto decoded = original;
        assert(TVPWebDecodePsbHeader(decoded.data(), decoded.size()));
        if(original[6] == 0 && original[7] == 0) {
            assert(decoded == original);
            continue;
        }
        const std::size_t length = original[4] == 4 ? 56 : 44;
        assert(decoded[6] == 0 && decoded[7] == 0);
        assert(TVPWebPsbHeaderDetail::valid(decoded.data(), length,
                                           decoded.size()));
        assert(!std::equal(original.begin(), original.begin() + length,
                           decoded.begin()));
        assert(std::equal(original.begin() + length, original.end(),
                          decoded.begin() + length));
        auto repeated = decoded;
        assert(TVPWebDecodePsbHeader(repeated.data(), repeated.size()));
        assert(repeated == decoded);
        // Corrupt only the existing input, without manufacturing a PSB fixture.
        for(std::size_t at = 8; at < length; ++at) {
            auto corrupted = original;
            corrupted[at] ^= 1;
            const auto before = corrupted;
            assert(!TVPWebDecodePsbHeader(corrupted.data(), corrupted.size()));
            assert(corrupted == before);
        }
        for(std::size_t size = 8; size < length; ++size) {
            auto truncated = original;
            assert(!TVPWebDecodePsbHeader(truncated.data(), size));
            assert(truncated == original);
        }
    }
}
