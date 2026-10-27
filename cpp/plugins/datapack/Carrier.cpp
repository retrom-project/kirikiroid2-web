#include "Reader.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

namespace DataPack {
namespace {

std::uint32_t ReadBigEndian32(const std::uint8_t *bytes) {
    return (std::uint32_t(bytes[0]) << 24) |
           (std::uint32_t(bytes[1]) << 16) |
           (std::uint32_t(bytes[2]) << 8) | std::uint32_t(bytes[3]);
}

std::uint16_t ReadBigEndian16(const std::uint8_t *bytes) {
    return (std::uint16_t(bytes[0]) << 8) | std::uint16_t(bytes[1]);
}

class PNGChunkStreamReader final : public Reader {
    std::unique_ptr<Reader> source;
    std::uint32_t remaining;

public:
    PNGChunkStreamReader(std::unique_ptr<Reader> reader, std::uint32_t size)
        : source(std::move(reader)), remaining(size) {}

    std::size_t Read(void *destination, std::size_t size) override {
        if (!source) return 0;
        const auto count = std::min(size, std::size_t(remaining));
        if (!source->Read(destination, count)) return 0;
        // The native reader treats every positive read as the full request.
        remaining -= static_cast<std::uint32_t>(count);
        return count;
    }
};

bool ProbePng(Reader &source, std::uint32_t &payloadSize) {
    constexpr std::array<std::uint8_t, 8> signature = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    std::array<std::uint8_t, 8> header{};
    if (source.Read(header.data(), header.size()) != header.size() ||
        header != signature)
        return false;

    while (source.Read(header.data(), header.size()) == header.size()) {
        const auto size = ReadBigEndian32(header.data());
        if (size & 0x80000000u) return false;
        if (std::memcmp(header.data() + 4, "tjSs", 4) == 0) {
            payloadSize = size;
            return true;
        }
        // Keep the native signed 32-bit addition before the 64-bit seek.
        const auto skip = static_cast<std::int32_t>(size + 4u);
        if (source.Seek(skip, SEEK_CUR) < 0) return false;
    }
    return false;
}

class StreamEscapeCharFilter : public Reader {
    std::uint8_t escape;
    std::uint8_t following;

    std::size_t Fill(std::size_t requested) {
        if (!ReadNextChunk(requested)) {
            buffer.resize(0);
            return 0;
        }
        const auto end = requested;
        std::size_t output = 0;
        for (std::size_t input = 0; input < end; ++input) {
            buffer[output] = buffer[input];
            if (buffer[input] == escape) {
                if (input + 1 >= end) {
                    // The PE32 filter reads into one byte beyond the raw chunk.
                    // Keep its stream consumption with a valid scratch element.
                    buffer.push_back(0);
                    if (source->Read(buffer.data() + end, 1) < 1)
                        buffer[end] = following;
                    ++requested;
                }
                const auto decoded = DecodeEscape(buffer.data() + input);
                if (decoded >= 0) {
                    ++input;
                    buffer[output] = static_cast<std::uint8_t>(decoded);
                }
            }
            ++output;
        }
        buffer.resize(output);
        return buffer.size();
    }

protected:
    std::unique_ptr<Reader> source;
    std::vector<std::uint8_t> buffer;

    StreamEscapeCharFilter(std::unique_ptr<Reader> reader,
                           std::uint8_t escapeByte,
                           std::uint8_t followingByte)
        : escape(escapeByte), following(followingByte),
          source(std::move(reader)) {}

    virtual bool ReadNextChunk(std::size_t &size) = 0;

    virtual int DecodeEscape(const std::uint8_t *input) const {
        return input[1] == following ? escape : -1;
    }

public:
    std::size_t Read(void *destination, std::size_t size) override {
        auto *output = static_cast<std::uint8_t *>(destination);
        std::size_t copied = 0;
        while (copied < size) {
            if (buffer.empty() && !Fill(size - copied)) break;
            const auto count = std::min(buffer.size(), size - copied);
            std::memcpy(output + copied, buffer.data(), count);
            copied += count;
            if (count == buffer.size()) {
                buffer.resize(0);
            } else {
                const auto remaining = buffer.size() - count;
                std::memmove(buffer.data(), buffer.data() + count, remaining);
                buffer.resize(remaining);
            }
        }
        return copied;
    }

    void SetBlockSize(std::size_t size) override {
        source->SetBlockSize(size);
        buffer.reserve(4 * size);
    }
};

class JPGChunkStreamReader final : public StreamEscapeCharFilter {
    bool ReadNextChunk(std::size_t &size) override {
        std::array<std::uint8_t, 4> header{};
        if (source->Read(header.data(), header.size()) != header.size() ||
            header[0] != 0xff || header[1] != 0xeb)
            return false;
        size = ReadBigEndian16(header.data() + 2);
        if (size < 2) return false;
        size -= 2;
        buffer.resize(size, 0);
        source->Read(buffer.data(), size);
        return true;
    }

public:
    explicit JPGChunkStreamReader(std::unique_ptr<Reader> reader)
        : StreamEscapeCharFilter(std::move(reader), 0xff, 0x00) {}

    ~JPGChunkStreamReader() override {
        // Native ownership cleanup precedes destruction of the filter buffer.
        source.reset();
    }
};

bool ProbeJpg(Reader &source) {
    std::array<std::uint8_t, 2> pair{};
    if (source.Read(pair.data(), pair.size()) != pair.size() ||
        pair[0] != 0xff || pair[1] != 0xd8)
        return false;

    if (source.Read(pair.data(), pair.size()) == pair.size()) {
        for (;;) {
            if (pair[0] != 0xff) return false;
            if (pair[1] == 0xda) break;
            if (source.Read(pair.data(), pair.size()) != pair.size())
                return false;
            const auto size = ReadBigEndian16(pair.data());
            if (size < 2) return false;
            if (source.Seek(size - 2, SEEK_CUR) < 0) return false;
            // The native probe also enters scanning after this partial read.
            if (source.Read(pair.data(), pair.size()) != pair.size()) break;
        }
    }

    std::vector<std::uint8_t> scan(0x4000, 0);
    for (;;) {
        const auto count = source.Read(scan.data(), 0x3fff);
        if (!count) return false;
        auto *cursor = scan.data();
        auto *end = scan.data() + count;
        while (cursor < end) {
            auto *marker = static_cast<std::uint8_t *>(
                std::memchr(cursor, 0xff, end - cursor));
            if (!marker) break;
            cursor = marker + 1;
            auto tail = end - cursor;
            if (cursor == end) {
                if (!source.Read(cursor, 1)) return false;
                ++tail;
            }
            if (*cursor == 0xeb) {
                source.Seek(-1 - static_cast<std::int64_t>(tail), SEEK_CUR);
                return true;
            }
            if (*cursor == 0xd9) return false;
        }
    }
}

} // namespace

bool UnwrapCarrier(std::unique_ptr<Reader> &reader) {
    if (!reader) return false;
    const auto position = reader->Seek(0, SEEK_CUR);
    std::array<std::uint8_t, 4> prefix{};
    const auto count = reader->Read(prefix.data(), prefix.size());
    if (position >= 0) reader->Seek(position, SEEK_SET);
    if (count != prefix.size()) return false;

    if (std::memcmp(prefix.data(), "TJS/", 4) == 0 ||
        std::memcmp(prefix.data(), "TJS\\", 4) == 0)
        return true;

    constexpr std::array<std::uint8_t, 4> png = {0x89, 0x50, 0x4e, 0x47};
    if (prefix == png) {
        std::uint32_t payloadSize = 0;
        if (!ProbePng(*reader, payloadSize)) return false;
        reader = std::make_unique<PNGChunkStreamReader>(std::move(reader),
                                                       payloadSize);
        return true;
    }

    constexpr std::array<std::uint8_t, 4> jpg = {0xff, 0xd8, 0xff, 0xe0};
    if (prefix != jpg || !ProbeJpg(*reader)) return false;
    reader = std::make_unique<JPGChunkStreamReader>(std::move(reader));
    return true;
}

} // namespace DataPack
