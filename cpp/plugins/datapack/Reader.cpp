#include "Reader.h"
#include "Crypt.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <lz4.h>

namespace DataPack {
namespace {

std::uint32_t Number(const std::uint8_t *data, unsigned size, bool bigendian) {
    std::uint32_t value = 0;
    for(unsigned i = 0; i < size; ++i) {
        const unsigned index = bigendian ? i : size - i - 1;
        value = (value << 8) | data[index];
    }
    return value;
}

class StreamCryptReaderWriter final : public Reader {
    std::unique_ptr<Reader> source;
    std::unique_ptr<BasicCryptFilter> filter;

public:
    StreamCryptReaderWriter(std::unique_ptr<Reader> reader,
                            const Options &options) : source(std::move(reader)) {
        if(options.cryptmode >= 1 && options.cryptmode <= 6) {
            const auto &iv = options.outeriv.empty() ? options.iv : options.outeriv;
            filter = std::make_unique<BasicCryptFilter>(options.seed, iv,
                                                       options.cryptmode);
        }
    }

    std::size_t Read(void *buffer, std::size_t size) override {
        const auto count = source->Read(buffer, size);
        if(filter)
            filter->Transform(buffer, count);
        return count;
    }

    void SetBlockSize(std::size_t size) override { source->SetBlockSize(size); }
};

class LZ4DecompressStream final : public Reader {
    std::unique_ptr<Reader> source;
    std::array<std::vector<char>, 2> output;
    std::vector<char> packed;
    LZ4_streamDecode_t decode{};
    int current = 1;
    int blockSize = 0;
    int packedLimit = 0;

public:
    explicit LZ4DecompressStream(std::unique_ptr<Reader> reader) :
        source(std::move(reader)) {
        LZ4_setStreamDecode(&decode, nullptr, 0);
    }

    void SetBlockSize(std::size_t size) override {
        source->SetBlockSize(size);
        current = 1;
        blockSize = static_cast<int>(size);
        packedLimit = LZ4_compressBound(blockSize);
        output[0].resize(size);
        output[1].resize(size);
        packed.resize(packedLimit + 2);
    }

    std::size_t Read(void *buffer, std::size_t size) override {
        std::uint8_t prefix[2];
        if(source->Read(prefix, 2) != 2)
            return 0;
        const auto length = Number(prefix, 2, false);
        if(!length || length > static_cast<unsigned>(packedLimit) ||
           source->Read(packed.data(), length) != length)
            return 0;
        current = 1 - current;
        const auto count = LZ4_decompress_safe_continue(
            &decode, packed.data(), output[current].data(), length, blockSize);
        if(count <= 0 || static_cast<std::size_t>(count) > size)
            return 0;
        std::memcpy(buffer, output[current].data(), count);
        return count;
    }
};

} // namespace

void ReadExact(Reader &reader, void *buffer, std::size_t size) {
    if(reader.Read(buffer, size) != size)
        throw std::runtime_error("malformed data");
}

void ReadHeader(Reader &reader, Options &options) {
    std::array<std::uint8_t, 16> header;
    ReadExact(reader, header.data(), header.size());
    Options parsed;
    if(std::memcmp(header.data(), "TJS/", 4) == 0)
        parsed.bigendian = false;
    else if(std::memcmp(header.data(), "TJS\\", 4) == 0)
        parsed.bigendian = true;
    else
        throw std::runtime_error("malformed data");
    if(header[5] != 's' || header[6] != '0' || header[7] != 0 ||
       (header[4] != 'n' && header[4] != '4'))
        throw std::runtime_error("malformed data");
    parsed.compress = header[4] == '4' ? 1 : 0;
    parsed.seed = Number(header.data() + 8, 4, parsed.bigendian);
    parsed.cryptmode = Number(header.data() + 12, 2, parsed.bigendian);
    const auto ivSize = Number(header.data() + 14, 2, parsed.bigendian);
    parsed.iv.resize(ivSize);
    if(ivSize)
        ReadExact(reader, parsed.iv.data(), ivSize);
    parsed.outeriv = options.outeriv;
    parsed.nocheck = options.nocheck;
    options = std::move(parsed);
}

std::unique_ptr<Reader> MakeReadFilters(std::unique_ptr<Reader> source,
                                      const Options &options) {
    if(options.cryptmode)
        source = std::make_unique<StreamCryptReaderWriter>(std::move(source), options);
    if(options.compress)
        source = std::make_unique<LZ4DecompressStream>(std::move(source));
    return source;
}

ArchiveStreamBuf::ArchiveStreamBuf(Reader &reader, std::size_t blockSize) :
    source(reader), buffer(blockSize) {
    source.SetBlockSize(blockSize);
    setg(buffer.data(), buffer.data(), buffer.data());
}

ArchiveStreamBuf::int_type ArchiveStreamBuf::underflow() {
    if(gptr() < egptr())
        return traits_type::to_int_type(*gptr());
    const auto count = source.Read(buffer.data(), buffer.size());
    if(!count)
        return traits_type::eof();
    setg(buffer.data(), buffer.data(), buffer.data() + count);
    return traits_type::to_int_type(*gptr());
}

} // namespace DataPack
