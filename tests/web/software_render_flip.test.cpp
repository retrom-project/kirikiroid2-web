#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <vector>

using tjs_int = int;
using tjs_uint8 = std::uint8_t;
using tjs_uint32 = std::uint32_t;
namespace TVPTextureFormat { enum e { Gray, RGBA }; }
struct tTVPRect {
    int left, top, right, bottom;
    int get_width() const { return right - left; }
    int get_height() const { return bottom - top; }
};

struct iTVPTexture2D {
    int width, height, bytesPerPixel;
    TVPTextureFormat::e format;
    std::vector<tjs_uint8> pixels;
    static inline int live = 0;
    iTVPTexture2D(int w, int h, TVPTextureFormat::e f) :
        width(w), height(h), bytesPerPixel(f == TVPTextureFormat::Gray ? 1 : 4),
        format(f), pixels(w * h * bytesPerPixel) { ++live; }
    virtual ~iTVPTexture2D() { --live; }
    void Release() { delete this; }
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    TVPTextureFormat::e GetFormat() const { return format; }
    void *GetScanLineForWrite(int y) { return pixels.data() + y * width * bytesPerPixel; }
    const void *GetScanLineForRead(int y) const { return pixels.data() + y * width * bytesPerPixel; }
};
struct tTVPSoftwareTexture2D : iTVPTexture2D {
    tTVPSoftwareTexture2D(iTVPTexture2D *source, int w, int h) :
        iTVPTexture2D(w, h, source->format) { pixels = source->pixels; }
};
struct tTVPRenderMethod_Software {
    virtual ~tTVPRenderMethod_Software() = default;
    virtual void DoRender(iTVPTexture2D *, const tTVPRect &, iTVPTexture2D *,
                          const tTVPRect &, iTVPTexture2D *, const tTVPRect &,
                          iTVPTexture2D *, const tTVPRect &) = 0;
};
int GetAdaptiveThreadNum(int, int) { return 1; }
template <typename F> void TVPExecThreadTask(int count, F f) {
    for(int i = 0; i < count; ++i) f(i);
}
void TVPReverse8(tjs_uint8 *row, int width) { std::reverse(row, row + width); }
void TVPReverse32(tjs_uint32 *row, int width) { std::reverse(row, row + width); }

#include "production-copy.h"

int main(int argc, char **argv) {
    assert(argc == 4);
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<tjs_uint8> rgba((std::istreambuf_iterator<char>(input)), {});
    const int width = std::stoi(argv[2]), height = std::stoi(argv[3]);
    assert(rgba.size() == static_cast<std::size_t>(width * height * 4));
    bool symmetric = true;
    for(int y = 0; y < height; ++y)
        symmetric = symmetric && std::equal(rgba.begin() + y * width * 4,
            rgba.begin() + (y + 1) * width * 4,
            rgba.begin() + (height - y - 1) * width * 4);
    assert(!symmetric);
    tTVPRenderMethod_DirectCopy copy;
    for(auto format : {TVPTextureFormat::Gray, TVPTextureFormat::RGBA}) {
        for(bool alias : {false, true}) {
            for(bool partial : {false, true}) {
                iTVPTexture2D source(width, height, format);
                iTVPTexture2D separate(width, height, format);
                for(std::size_t i = 0; i < source.pixels.size(); ++i)
                    source.pixels[i] = rgba[format == TVPTextureFormat::Gray ? i * 4 : i];
                separate.pixels = source.pixels;
                auto *destination = alias ? &source : &separate;
                const auto original = source.pixels;
                auto expected = destination->pixels;
                const tTVPRect rect = partial ? tTVPRect{7, 11, width - 9, height - 12}
                                              : tTVPRect{0, 0, width, height};
                const tTVPRect flipped{rect.left, rect.bottom, rect.right, rect.top};
                const int pitch = width * source.bytesPerPixel;
                const int x = rect.left * source.bytesPerPixel;
                const int rowBytes = rect.get_width() * source.bytesPerPixel;
                for(int y = rect.top; y < rect.bottom; ++y)
                    std::memcpy(expected.data() + y * pitch + x,
                                original.data() + (rect.bottom - 1 - y + rect.top) * pitch + x,
                                rowBytes);
                const int before = iTVPTexture2D::live;
                copy.DoRender(destination, rect, destination, rect, &source,
                              flipped, nullptr, flipped);
                if(destination->pixels != expected) {
                    std::cerr << "vertical copy corrupted source: alias=" << alias
                              << " partial=" << partial << " bpp=" << source.bytesPerPixel << '\n';
                    return 1;
                }
                assert(iTVPTexture2D::live == before);
                if(!alias) assert(source.pixels == original);
                else {
                    copy.DoRender(destination, rect, destination, rect, &source,
                                  flipped, nullptr, flipped);
                    assert(destination->pixels == original);
                }
            }
        }
    }
}
