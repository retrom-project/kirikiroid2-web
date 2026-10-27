#ifndef KRKR2_DATAPACK_READER_H
#define KRKR2_DATAPACK_READER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <streambuf>
#include <string>
#include <vector>

namespace DataPack {

class Reader {
public:
    virtual ~Reader() = default;
    virtual std::size_t Read(void *buffer, std::size_t size) = 0;
    virtual void SetBlockSize(std::size_t) {}
    virtual std::int64_t Seek(std::int64_t, int) { return -1; }
};

void ReadExact(Reader &reader, void *buffer, std::size_t size);
bool UnwrapCarrier(std::unique_ptr<Reader> &reader);

struct Options {
    bool nocheck = false;
    bool checkfail = false;
    bool bigendian = false;
    int compress = 0;
    int cryptmode = 0;
    std::uint32_t seed = 0;
    std::string iv;
    std::string outeriv;
};

void ReadHeader(Reader &reader, Options &options);
std::unique_ptr<Reader> MakeReadFilters(std::unique_ptr<Reader> source,
                                      const Options &options);

class ArchiveStreamBuf final : public std::streambuf {
    Reader &source;
    std::vector<char> buffer;

protected:
    int_type underflow() override;

public:
    ArchiveStreamBuf(Reader &reader, std::size_t blockSize);
};

} // namespace DataPack

#endif
