#include "GfxFire.h"
#include "GfxFireColors.h"
#include <algorithm>
#include <cmath>

namespace {
std::uint32_t CustomColors[256];
std::uint8_t roundFour(unsigned value) { return (value + (value & 3)) >> 2; }
void horizontal(const std::uint8_t *source, std::uint8_t *dest, int width) {
    for(int x = 1; x < width - 1; ++x)
        dest[x] = roundFour(source[x - 1] + 2 * source[x] + source[x + 1]);
    dest[0] = roundFour(3 * source[0] + source[1]);
    dest[width - 1] = roundFour(3 * source[width - 1] + source[width - 2]);
}
}

void GfxLayer::bind(iTJSDispatch2 *object) {
    Object = object;
    NI_LayerExBase::init(object);
    reset(object);
    static bool initialized = false;
    static iTJSDispatch2 *left, *top;
    tTJSVariant value;
    if(!initialized) {
        initialized = true;
        object->PropGet(TJS_IGNOREPROP, TJS_W("left"), nullptr, &value, object);
        left = value.AsObject();
        object->PropGet(TJS_IGNOREPROP, TJS_W("top"), nullptr, &value, object);
        top = value.AsObject();
    }
    _leftProp->PropGet(0, nullptr, nullptr, &value, object);
    ImageLeft = tjs_int(value);
    _topProp->PropGet(0, nullptr, nullptr, &value, object);
    ImageTop = tjs_int(value);
    left->PropGet(0, nullptr, nullptr, &value, object);
    Left = tjs_int(value);
    top->PropGet(0, nullptr, nullptr, &value, object);
    Top = tjs_int(value);
}

gfxFire::gfxFire() : Palette(gfx_colors::Red),
    Random(motion::EmoteBlinkRng_getGlobal_guess()) {}

tjs_error gfxFire::init(tTJSVariant *, tjs_int argc, tTJSVariant **argv, gfxFire *self) {
    if(argc < 1) return TJS_E_BADPARAMCOUNT;
    int count = argc == 1 ? 1 : tjs_int(*argv[1]);
    self->bind(argv[0]->AsObjectNoAddRef());
    self->Seeds.resize(count);
    for(int i = 0; i < count; ++i) self->Seeds[i] = new Seed;
    self->allocate();
    return TJS_S_OK;
}
tjs_error gfxFire::clearFireSeed(tTJSVariant *, tjs_int argc, tTJSVariant **argv, gfxFire *self) {
    int index = argc < 1 ? 0 : tjs_int(*argv[0]);
    if(index >= 0 && index < int(self->Seeds.size())) self->Seeds[index]->clear();
    return TJS_S_OK;
}
tjs_error gfxFire::setFireSeed(tTJSVariant *, tjs_int argc, tTJSVariant **argv, gfxFire *self) {
    if(argc < 1) return TJS_E_BADPARAMCOUNT;
    int index = argc == 1 ? 0 : tjs_int(*argv[1]);
    self->SeedLayer.bind(argv[0]->AsObjectNoAddRef());
    self->copySeed(index);
    return TJS_S_OK;
}
tjs_error gfxFire::setFireSeedPos(tTJSVariant *, tjs_int argc, tTJSVariant **argv, gfxFire *self) {
    if(argc < 2) return TJS_E_BADPARAMCOUNT;
    int index = argc == 2 ? 0 : tjs_int(*argv[2]);
    if(index >= 0 && index < int(self->Seeds.size())) {
        self->Seeds[index]->X = tjs_int(*argv[0]);
        self->Seeds[index]->Y = tjs_int(*argv[1]);
    }
    return TJS_S_OK;
}

void gfxFire::applyRed() { Palette = gfx_colors::Red; }
void gfxFire::applyBlue() { Palette = gfx_colors::Blue; }
void gfxFire::applyWhite() { Palette = gfx_colors::White; }
void gfxFire::applyCustom() { Palette = CustomColors; }
bool gfxFire::isRed() const { return Palette == gfx_colors::Red; }
bool gfxFire::isBlue() const { return Palette == gfx_colors::Blue; }
bool gfxFire::isWhite() const { return Palette == gfx_colors::White; }
void gfxFire::setCustomColorTable(iTJSDispatch2 *layer) {
    GfxLayer view;
    view.bind(layer);
    if(view._width >= 256 && view._height >= 1)
        std::memcpy(CustomColors, view._buffer, sizeof(CustomColors));
    applyCustom();
}

void gfxFire::allocate() {
    Pixels.borrow(reinterpret_cast<std::uint32_t *>(_buffer), _width, _height, _pitch);
    Heat.resize(_width, _height);
    Work.resize(_width, _height);
    int width = _width / 16 + 1, height = _height / 16 + 1;
    GridX.resize(width, height); GridY.resize(width, height);
    VelocityX.resize(width, height); VelocityY.resize(width, height);
    updateWarpMap();
    allocateSpans();
    updateCoolMap();
}

void gfxFire::updateWarpMap() {
    int width = GridX.Width, height = GridX.Height;
    for(int x = 0; x < width; ++x) {
        GridX.Rows[0][x] = GridX.Rows[height - 1][x] = float(16 * x);
        GridY.Rows[0][x] = 0; GridY.Rows[height - 1][x] = float(16 * (height - 1));
        VelocityX.Rows[0][x] = VelocityY.Rows[0][x] = 0;
        VelocityX.Rows[height - 1][x] = VelocityY.Rows[height - 1][x] = 0;
    }
    for(int y = 1; y < height - 1; ++y) {
        GridX.Rows[y][0] = 0; GridX.Rows[y][width - 1] = float(16 * (width - 1));
        GridY.Rows[y][0] = GridY.Rows[y][width - 1] = float(16 * y);
        VelocityX.Rows[y][0] = VelocityY.Rows[y][0] = 0;
        VelocityX.Rows[y][width - 1] = VelocityY.Rows[y][width - 1] = 0;
        for(int x = 1; x < width - 1; ++x) {
            GridX.Rows[y][x] = float(16 * x - ForceH - BoundH / 2 + random() * BoundH);
            GridY.Rows[y][x] = float(16 * y - ForceV - BoundV / 2 + random() * BoundV);
            VelocityX.Rows[y][x] = VelocityY.Rows[y][x] = 0;
        }
    }
}

void gfxFire::updateCoolMap() {
    Cool.resize(Pixels.Width, Pixels.Height);
    int width = Cool.Width, height = Cool.Height;
    int count = DensityNumerator * width * height / DensityDenominator;
    for(int i = 0; i < count; ++i) {
        unsigned index = unsigned(double(width * height) * random());
        Cool.Rows[index / unsigned(width)][index % unsigned(width)] =
            std::uint8_t(int(random() * CoolRange + CoolStrength));
    }
    for(int i = 0; i < Blur; ++i) {
        horizontal(Cool.Rows[0], Work.Rows[0], width);
        horizontal(Cool.Rows[1], Work.Rows[1], width);
        for(int y = 1; y < height - 1; ++y) {
            horizontal(Cool.Rows[y + 1], Work.Rows[y + 1], width);
            for(int x = 0; x < width; ++x)
                Cool.Rows[y][x] = roundFour(Work.Rows[y - 1][x] +
                    2 * Work.Rows[y][x] + Work.Rows[y + 1][x]);
        }
        for(int x = 0; x < width; ++x) {
            Cool.Rows[0][x] = roundFour(3 * Work.Rows[0][x] + Work.Rows[1][x]);
            // The native traversal writes the second boundary result to row 1.
            Cool.Rows[1][x] = roundFour(3 * Work.Rows[height - 1][x] + Work.Rows[height - 2][x]);
        }
    }
}

void gfxFire::copySeed(int index) {
    if(index < 0 || index >= int(Seeds.size())) return;
    GfxArray<std::uint32_t> pixels;
    pixels.borrow(reinterpret_cast<std::uint32_t *>(SeedLayer._buffer),
                  SeedLayer._width, SeedLayer._height, SeedLayer._pitch);
    Seed &seed = *Seeds[index];
    seed.resize(pixels.Width, pixels.Height);
    for(int y = 0; y < pixels.Height; ++y)
        for(int x = 0; x < pixels.Width; ++x) seed.Rows[y][x] = pixels.Rows[y][x] >> 24;
}

bool gfxFire::clip(Rect &dest, Rect &source, int dw, int dh, int sw, int sh) {
    if(source.Left >= sw || source.Top >= sh || source.Right < 1 || source.Bottom < 1 ||
       dest.Left >= dw || dest.Top >= dh || dest.Right < 1 || dest.Bottom < 1) return false;
    int destWidth = dest.Right - dest.Left, sourceWidth = source.Right - source.Left;
    if(dest.Left >= 0) {
        source.Left -= dest.Left * sourceWidth / destWidth; dest.Left = 0;
    }
    int sourceHeight = source.Bottom - source.Top, destHeight = dest.Bottom - dest.Top;
    if(dest.Top < 0) {
        source.Top -= dest.Top * sourceHeight / destHeight; dest.Top = 0;
    }
    if(dest.Right > dw) {
        source.Right -= (dest.Right - dw) * sourceWidth / destWidth; dest.Right = dw;
    }
    if(dest.Bottom > dh) {
        source.Bottom -= (dest.Bottom - dh) * sourceHeight / destHeight; dest.Bottom = dh;
    }
    if(source.Left < 0) {
        dest.Left -= source.Left * destWidth / sourceWidth; source.Left = 0;
    }
    if(source.Top < 0) {
        dest.Top -= source.Top * destHeight / sourceHeight; source.Top = 0;
    }
    if(source.Right > sw) {
        dest.Right -= (source.Right - sw) * destWidth / sourceWidth; source.Right = sw;
    }
    if(source.Bottom > sh) {
        dest.Bottom -= (source.Bottom - sh) * destHeight / sourceHeight; source.Bottom = sh;
    }
    return dest.Right > dest.Left && dest.Bottom > dest.Top &&
           source.Right > source.Left && source.Bottom > source.Top;
}
void gfxFire::addSeeds() {
    for(Seed *seed : Seeds) {
        if(seed->Width < 1 || seed->Height < 1) continue;
        Rect dest{seed->X, seed->Y, seed->X + seed->Width, seed->Y + seed->Height};
        Rect source{0, 0, seed->Width, seed->Height};
        if(!clip(dest, source, Heat.Width, Heat.Height, seed->Width, seed->Height)) continue;
        for(int y = dest.Top, sy = source.Top; y < dest.Bottom; ++y, ++sy) {
            auto *out = Heat.Rows[y] + dest.Left;
            const auto *in = seed->Rows[sy] + source.Left;
            for(int x = 0; x < source.Right - source.Left; ++x)
                out[x] = std::min(255, int(out[x]) + in[x]);
        }
    }
}

void gfxFire::advanceGrid() {
    static const float rest[3][3] = {{0, 16, 32}, {16, 22.6274166107f, 35.7770881653f},
                                   {32, 35.7770881653f, 45.2548332214f}};
    for(int y = 2; y < GridX.Height - 2; ++y) {
        for(int x = 2; x < GridX.Width - 2; ++x) {
            for(int ny = y - 2; ny <= y + 2; ++ny) {
                for(int nx = x - 2; nx <= x + 2; ++nx) {
                    if(nx == x && ny == y) continue;
                    float dx = GridX.Rows[ny][nx] - GridX.Rows[y][x];
                    float dy = GridY.Rows[ny][nx] - GridY.Rows[y][x];
                    float force = (rest[std::abs(ny - y)][std::abs(nx - x)] -
                                   std::sqrt(dx * dx + dy * dy)) * Scaling;
                    VelocityX.Rows[ny][nx] += dx * force;
                    VelocityY.Rows[ny][nx] += dy * force;
                }
            }
        }
    }
    for(int y = 1; y < GridX.Height - 1; ++y)
        for(int x = 1; x < GridX.Width - 1; ++x) {
            GridX.Rows[y][x] += VelocityX.Rows[y][x];
            GridY.Rows[y][x] += VelocityY.Rows[y][x];
        }
}

void gfxFire::allocateSpans() {
    delete[] LeftBoundary; delete[] RightBoundary;
    if(Blocks) {
        for(int i = 0; i < BlockCount; ++i) delete[] Blocks[i];
        delete[] Blocks;
    }
    int height = 16 * (GridX.Height - 1), width = 16 * GridX.Width;
    BlockCount = height / 8;
    LeftBoundary = new float[3 * height + 24];
    RightBoundary = new float[24 * BlockCount];
    Blocks = new float *[BlockCount];
    for(int i = 0; i < BlockCount; ++i)
        Blocks[i] = new float[(24 * GridX.Width + 24) * (GridX.Height - 1)];
    EdgeCounts.resize(height);
    boundary({0, float(height)}, {-.5f, height - .5f}, {-.5f, -.5f}, {0, 0}, false);
    boundary({float(width), 0}, {width - .5f, -.5f},
             {width - .5f, height - .5f}, {float(width), float(height)}, true);
}
void gfxFire::boundary(const Point &source0, const Point &dest0,
                       const Point &dest1, const Point &source1, bool right) {
    Point start = right ? dest0 : dest1, finish = right ? dest1 : dest0;
    Point source = right ? source0 : source1, target = right ? source1 : source0;
    float dx, du, dv;
    if(right) {
        float height = std::max(finish.Y - start.Y, .01f);
        dx = (finish.X - start.X) / height;
        du = (target.X - source.X) / height; dv = (target.Y - source.Y) / height;
    } else {
        double height = std::max(double(finish.Y - start.Y), .01);
        dx = float((finish.X - start.X) / height);
        du = float((target.X - source.X) / height); dv = float((target.Y - source.Y) / height);
    }
    float x = start.X, u = source.X, v = source.Y;
    if(start.Y < 0) {x -= start.Y * dx; u -= start.Y * du; v -= start.Y * dv;}
    int first = start.Y > 0 ? int(start.Y) + 1 : 0;
    int last = std::min(int(finish.Y), int(EdgeCounts.size()) - 1);
    for(int row = first; ; ++row) {
        if(right) {
            float *out = RightBoundary + 3 * row;
            out[0] = x; out[1] = u; out[2] = v;
        } else {
            float *out = LeftBoundary + (row & 7) + 24 * (row / 8);
            out[0] = x; out[8] = u; out[16] = v;
        }
        if(row >= last) break;
        x += dx;
        if(right) {u = dv + (du + u); v = u;} else {u += du; v += dv;}
    }
}
void gfxFire::edge(const Point &source0, const Point &dest0,
                   const Point &dest1, const Point &source1) {
    float height = std::max(dest1.Y - dest0.Y, .01f);
    float dx = (dest1.X - dest0.X) / height;
    float du = (source1.X - source0.X) / height, dv = (source1.Y - source0.Y) / height;
    float x = dest0.X, u = source0.X, v = source0.Y;
    if(dest0.Y < 0) {x -= dest0.Y * dx; u -= dest0.Y * du; v -= dest0.Y * dv;}
    int first = dest0.Y > 0 ? int(dest0.Y) + 1 : 0;
    int last = std::min(int(dest1.Y), int(EdgeCounts.size()) - 1);
    if(first >= last) return;
    for(int row = first; row <= last; ++row) {
        float *out = span(row, EdgeCounts[row]++);
        out[0] = x; out[8] = u; out[16] = v;
        x += dx; u += du; v += dv;
    }
}
void gfxFire::buildSpans() {
    std::fill(EdgeCounts.begin(), EdgeCounts.end(), 1);
    for(int i = 0; i < BlockCount; ++i) std::memcpy(Blocks[i], LeftBoundary + 24 * i, 96);
    for(int x = 1; x < GridX.Width - 1; ++x) {
        for(int y = 1; y < GridX.Height - 1; ++y)
            edge({float((x - 1) * 16), float(y * 16)},
                 {GridX.Rows[y][x - 1] - .5f, GridY.Rows[y][x - 1] - .5f},
                 {GridX.Rows[y][x] - .5f, GridY.Rows[y][x] - .5f},
                 {float(x * 16), float(y * 16)});
        for(int y = 0; y < GridX.Height - 1; ++y)
            edge({float(x * 16), float(y * 16)},
                 {GridX.Rows[y][x] - .5f, GridY.Rows[y][x] - .5f},
                 {GridX.Rows[y + 1][x] - .5f, GridY.Rows[y + 1][x] - .5f},
                 {float(x * 16), float((y + 1) * 16)});
    }
    for(int y = 0; y < int(EdgeCounts.size()); ++y) {
        float *out = span(y, EdgeCounts[y]++), *in = RightBoundary + 3 * y;
        out[0] = in[0]; out[8] = in[1]; out[16] = in[2];
    }
}
void gfxFire::sortSpans(int row, int count) {
    for(int i = 1; i < count; ++i)
        for(int j = i; j > 0 && span(row, j)[0] < span(row, j - 1)[0]; --j) {
            float *a = span(row, j), *b = span(row, j - 1);
            std::swap(a[0], b[0]); std::swap(a[8], b[8]); std::swap(a[16], b[16]);
        }
}
void gfxFire::textureSpan(float &left, float &right, float &u0, float u1,
                          float &v0, float v1, int width, int &u, int &v, int &du, int &dv) {
    float dx = std::max(right - left, .01f);
    float stepU = (u1 - left) / dx, stepV = (v1 - v0) / dx;
    if(left < 0) {u0 -= left * stepU; v0 -= left * stepV; left = 0;}
    if(right > width) right = float(width);
    u = int(double(u0) * 65536); v = int(double(v0) * 65536);
    du = int(double(stepU) * 65536); dv = int(double(stepV) * 65536);
}
std::uint8_t gfxFire::sampleFiltered(int u, int v, int stride, unsigned size) const {
    unsigned index = unsigned((v >> 16) * stride + (u >> 16));
    auto read = [&](unsigned offset) -> unsigned { return offset < size ? Heat.Data[offset] : 0; };
    unsigned x = u & 65535, y = v & 65535;
    unsigned top = (read(index) * (65535 - x) + read(index + 1) * x) >> 16;
    unsigned bottom = (read(index + stride) * (65535 - x) + read(index + stride + 1) * x) >> 16;
    return (top * (65535 - y) + bottom * y) >> 16;
}
void gfxFire::sample(bool filtered) {
    int stride = Heat.pitch();
    unsigned size = Heat.Height * stride;
    int rows = filtered ? Work.Height : int(EdgeCounts.size());
    for(int row = 0; row < rows; ++row) {
        int count = EdgeCounts[row];
        sortSpans(row, count);
        for(int i = 1; i < count; ++i) {
            const float *a = span(row, i - 1), *b = span(row, i);
            float left = a[0], right = b[0], u0 = a[8], v0 = a[16];
            int u, v, du, dv;
            textureSpan(left, right, u0, b[8], v0, b[16], Work.Width, u, v, du, dv);
            for(int x = int(left); x < int(right); ++x) {
                unsigned index = unsigned((v >> 16) * stride + (u >> 16));
                Work.Rows[row][x] = filtered ? sampleFiltered(u, v, stride, size) : index < size ? Heat.Data[index] : 0;
                u = int(unsigned(u) + unsigned(du)); v = int(unsigned(v) + unsigned(dv));
            }
        }
    }
}
void gfxFire::coolAndColor() {
    int width = Work.Width / 16 * 16;
    for(int y = 0; y < Work.Height; ++y) {
        for(int x = 0; x < width; ++x)
            Heat.Rows[y][x] = std::max(0, int(Work.Rows[y][x]) - Cool.Rows[y][x]);
        horizontal(Heat.Rows[y], Work.Rows[y], width);
        if(y > 1) {
            for(int x = 0; x < width; ++x) {
                auto value = roundFour(Work.Rows[y - 2][x] + 2 * Work.Rows[y - 1][x] + Work.Rows[y][x]);
                Heat.Rows[y - 2][x] = value;
                Pixels.Rows[y - 2][x] = Palette[value];
            }
        }
    }
    for(int y = Work.Height - 2; y < Work.Height; ++y) {
        std::memset(Heat.Rows[y], 0, width);
        std::memset(Pixels.Rows[y], 0, width * sizeof(std::uint32_t));
    }
}
void gfxFire::cycle() {
    addSeeds();
    advanceGrid();
    buildSpans();
    sample(Filter != 0);
    coolAndColor();
    redraw(Object);
}
