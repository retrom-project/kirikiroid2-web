// GlitchEffect.dll is a game-supplied Layer extension that also registers the
// glitch, fadeglitch and loopglitch transitions. The four Kirikiroid2
// references contain no such module (no ASCII or UTF-16 "glitch" member names),
// so this Web extension follows the supplied plugin's own control flow; mapping
// and addresses are recorded in analysis/glitch-effect.md.
#define NCB_MODULE_NAME TJS_W("GlitchEffect.dll")
#include "ncbind.hpp"
#include "TickCount.h"
#include "transhandler.h"
#include "TransIntf.h"
#include "RenderManager.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace {

struct GlitchRect {
    tjs_int left = 0, top = 0, right = 0, bottom = 0;
};

// Intersection writes the result into rect; an empty result clears it.
bool GlitchIntersect(GlitchRect &rect, const GlitchRect &bound) {
    tjs_int left = rect.left < bound.left ? bound.left : rect.left;
    tjs_int top = rect.top < bound.top ? bound.top : rect.top;
    tjs_int right = rect.right > bound.right ? bound.right : rect.right;
    tjs_int bottom = rect.bottom > bound.bottom ? bound.bottom : rect.bottom;
    if(right <= left || bottom <= top) {
        rect = GlitchRect();
        return false;
    }
    rect = {left, top, right, bottom};
    return true;
}

// The plugin only rejects an instance check that fails; false is still accepted.
bool GlitchIsLayer(iTJSDispatch2 *obj) {
    return obj && obj->IsInstanceOf(0, nullptr, nullptr, TJS_W("Layer"), obj) >= 0;
}

tTVInteger GlitchLayerInteger(iTJSDispatch2 *layer, const tjs_char *name, tTVInteger fallback) {
    tTJSVariant value;
    if(layer->PropGet(0, name, nullptr, &value, layer) >= 0) fallback = value.AsInteger();
    return fallback;
}

bool GlitchImageSize(iTJSDispatch2 *layer, tjs_int &width, tjs_int &height) {
    if(!GlitchIsLayer(layer) || !GlitchLayerInteger(layer, TJS_W("hasImage"), 0)) return false;
    tTVInteger w = GlitchLayerInteger(layer, TJS_W("imageWidth"), 0);
    if(w <= 0) return false;
    tTVInteger h = GlitchLayerInteger(layer, TJS_W("imageHeight"), 0);
    if(h <= 0) return false;
    width = (tjs_int)w;
    height = (tjs_int)h;
    return true;
}

bool GlitchPitch(iTJSDispatch2 *layer, tjs_int &pitch, bool main) {
    tTVInteger value = GlitchLayerInteger(
        layer, main ? TJS_W("mainImageBufferPitch") : TJS_W("provinceImageBufferPitch"), 0);
    if(!value) return false;
    pitch = (tjs_int)value;
    return true;
}

bool GlitchClipRect(iTJSDispatch2 *layer, GlitchRect &rect) {
    if(!GlitchIsLayer(layer)) return false;
    tjs_int left = (tjs_int)GlitchLayerInteger(layer, TJS_W("clipLeft"), 0);
    tjs_int top = (tjs_int)GlitchLayerInteger(layer, TJS_W("clipTop"), 0);
    tjs_int width = (tjs_int)GlitchLayerInteger(layer, TJS_W("clipWidth"), 0);
    rect.bottom = top + (tjs_int)GlitchLayerInteger(layer, TJS_W("clipHeight"), 0);
    rect.left = left;
    rect.top = top;
    rect.right = width + left;
    return true;
}

// Source rows are read relative to base; every segment is clamped into [min, max).
struct GlitchSource {
    tjs_uint8 *base = nullptr, *min = nullptr, *max = nullptr;
    tjs_int pitch = 0;

    void set(tjs_uint8 *origin, tjs_int stride, tjs_int width, tjs_int height) {
        base = min = max = origin;
        pitch = stride;
        if(width > 0 && height > 0) {
            std::ptrdiff_t last = (std::ptrdiff_t)stride * (height - 1);
            if(stride >= 0) {
                max = origin + last + 4 * width;
            } else {
                min = origin + last;
                max = origin + 4 * width;
            }
        }
    }

    // The plugin forms the address before clamping; offsets avoid out-of-object
    // pointer arithmetic while selecting the same bytes.
    const tjs_uint8 *clamped(std::ptrdiff_t offset, std::ptrdiff_t bytes) const {
        std::ptrdiff_t low = min - base, high = max - base;
        if(offset < low) return min;
        if(offset + bytes > high) return max - bytes;
        return base + offset;
    }
};

struct GlitchCopy {
    iTJSDispatch2 *dest, *src;
    GlitchRect rect;
    GlitchSource source;
    std::unique_ptr<tjs_uint8[]> duplicate;

    GlitchCopy(iTJSDispatch2 *destination, iTJSDispatch2 *image) : dest(destination), src(image) {}

    // Same-layer copies read from a duplicate of the source range.
    void separate() {
        std::size_t bytes = (std::size_t)(source.max - source.min);
        duplicate.reset(new tjs_uint8[bytes]);
        std::memmove(duplicate.get(), source.min, bytes);
        std::ptrdiff_t delta = duplicate.get() - source.min;
        source.base += delta;
        source.max += delta;
        source.min = duplicate.get();
    }

    tjs_error prepare(tjs_int dx, tjs_int dy, tjs_int sx, tjs_int sy, const tjs_int *w, const tjs_int *h);
    tjs_error apply(tTJSVariantClosure &elm);
};

// Returns 1 when the clipped area is empty.
tjs_error GlitchCopy::prepare(tjs_int dx, tjs_int dy, tjs_int sx, tjs_int sy, const tjs_int *w,
                              const tjs_int *h) {
    tjs_int srcWidth = 0, srcHeight = 0;
    if(!GlitchImageSize(src, srcWidth, srcHeight)) return TJS_E_INVALIDPARAM;
    tjs_int width = w ? *w : srcWidth;
    tjs_int height = h ? *h : srcHeight;
    GlitchRect area{sx, sy, width + sx, height + sy};
    GlitchRect target{0, 0, srcWidth, srcHeight};
    if(!GlitchIntersect(area, target)) return 1;
    target = {area.left - sx + dx, area.top - sy + dy, dx - sx + area.right, dy - sy + area.bottom};
    if(!GlitchClipRect(dest, area)) return TJS_E_FAIL;
    if(!GlitchIntersect(target, area)) return 1;
    // The plugin retains the destination clip rectangle, not the intersection.
    rect = area;
    tjs_int pitch = 0;
    auto *buffer = (tjs_uint8 *)(tjs_intptr_t)GlitchLayerInteger(src, TJS_W("mainImageBuffer"), 0);
    if(!buffer || !GlitchPitch(src, pitch, true)) return TJS_E_FAIL;
    source.set(buffer + (std::ptrdiff_t)sy * pitch + 4 * sx, pitch, rect.right - rect.left,
               rect.bottom - rect.top);
    if(dest == src) separate();
    return TJS_S_OK;
}

// The plugin's parameter block, read from the effect dictionary.
struct GlitchParams {
    double noise, sftX, sftY, sftCol, perReset, perX, perY, perCol;
};

struct GlitchDictionary {
    tTJSVariantClosure &elm;

    bool get(const tjs_char *name, tTJSVariant &value) const {
        if(!elm.Object) TJSThrowNullAccess();
        iTJSDispatch2 *objthis = elm.ObjThis ? elm.ObjThis : elm.Object;
        return elm.Object->PropGet(0, name, nullptr, &value, objthis) >= 0 && value.Type() != tvtVoid;
    }
    double real(const tjs_char *name, double fallback) const {
        tTJSVariant value;
        return get(name, value) ? (double)value.AsReal() : fallback;
    }
    tjs_int integer(const tjs_char *name, tjs_int fallback) const {
        tTJSVariant value;
        return get(name, value) ? (tjs_int)value : fallback;
    }
};

// The same defaults are read from a Layer effect dictionary and from
// transition options.
template <class Reader> GlitchParams GlitchReadParams(const Reader &reader) {
    GlitchParams result;
    result.noise = reader.real(TJS_W("noise"), 4.0);
    result.sftX = reader.real(TJS_W("sft_x"), 16.0);
    result.sftY = reader.real(TJS_W("sft_y"), 8.0);
    result.sftCol = reader.real(TJS_W("sft_col"), 8.0);
    result.perX = reader.real(TJS_W("per_x"), 0.5);
    result.perY = reader.real(TJS_W("per_y"), 0.25);
    result.perCol = reader.real(TJS_W("per_col"), 0.05);
    result.perReset = reader.real(TJS_W("per_reset"), 0.02);
    return result;
}

std::uint64_t GlitchSplitMix(std::uint64_t &state) {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

std::uint64_t GlitchRotl(std::uint64_t value, int count) {
    return (value << count) | (value >> (64 - count));
}

// xoroshiro128** seeded by two SplitMix64 values.
struct GlitchRandom {
    std::uint64_t s0 = 0, s1 = 0;

    std::uint64_t next() {
        std::uint64_t x = s0, y = s1 ^ s0, result = GlitchRotl(x * 5, 7) * 9;
        s0 = GlitchRotl(x, 24) ^ y ^ (y << 16);
        s1 = GlitchRotl(y, 37);
        return result;
    }
    // Mantissa construction yields [0, 1).
    double uniform() {
        std::uint64_t bits = (next() >> 12) | 0x3FF0000000000000ull;
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        return value - 1.0;
    }
};

// Polar Box-Muller returning the second value and caching the first.
struct GlitchNormal {
    GlitchRandom &random;
    double cached = 0.0;
    bool hasCached = false;

    double operator()() {
        if(hasCached) {
            hasCached = false;
            return cached;
        }
        double x, y, s;
        do {
            do {
                x = random.uniform() * 2.0 - 1.0;
                y = random.uniform() * 2.0 - 1.0;
                s = y * y + x * x;
            } while(s >= 1.0);
        } while(s == 0.0);
        double factor = std::sqrt(std::log(s) * -2.0 / s);
        hasCached = true;
        cached = factor * x;
        return factor * y;
    }
};

struct GlitchField {
    GlitchParams params;
    GlitchRandom random;
    std::vector<double> lines, blockX, blockY, blockCol;
    std::vector<std::uint64_t> lineMask;
    tjs_int size, columns, rows;

    GlitchField(const GlitchParams &values, tjs_int width, tjs_int height, tjs_int block)
        : params(values), size(block) {
        std::uint64_t seed = 0;
        random.s0 = GlitchSplitMix(seed);
        random.s1 = GlitchSplitMix(seed);
        columns = (block + width - 1) / block;
        rows = (height + block - 1) / block;
        tjs_int cells = columns * rows;
        lines.resize(height);
        lineMask.resize((height + 63) >> 6);
        blockX.resize(cells);
        blockY.resize(cells);
        blockCol.resize(cells);
    }

    void fillLines() {
        GlitchNormal normal{random};
        for(double &line : lines) line = normal() * params.noise + 0.0;
    }

    // Each cell carries a running offset: a jump adds a normal sample and a
    // reset multiplies the accumulated value by zero.
    void fillWalk(std::vector<double> &cells, double jump, double deviation) {
        GlitchNormal normal{random};
        double sum = 0.0;
        for(double &cell : cells) {
            if(jump > random.uniform()) sum = normal() * deviation + 0.0 + sum;
            double value = sum;
            if(params.perReset > random.uniform()) {
                value = sum * 0.0;
                sum = sum * 0.0;
            }
            cell = value;
        }
    }

    void fillBlocks() {
        fillWalk(blockX, params.perX, params.sftX);
        fillWalk(blockY, params.perY, params.sftY);
        fillWalk(blockCol, params.perCol, params.sftCol);
    }

    // The plugin indexes by absolute destination coordinates. A non-zero clip
    // origin reaches beyond its storage in the original; such reads use zero here.
    static double at(const std::vector<double> &values, std::ptrdiff_t index) {
        return index >= 0 && (std::size_t)index < values.size() ? values[(std::size_t)index] : 0.0;
    }

    void render(tjs_uint8 *dest, tjs_int destPitch, const GlitchSource &source, double coef,
                const GlitchRect &area) const;

    // Transition frames reseed the generator from the tick.
    void reseed(std::uint64_t tick) {
        random.s0 = GlitchSplitMix(tick);
        random.s1 = GlitchSplitMix(tick);
    }

    // Each row switches to the second image with probability progress.
    void fillMask(double progress) {
        for(std::uint64_t &word : lineMask) {
            std::uint64_t bits = 0;
            for(std::uint64_t bit = 1; bit; bit <<= 1) {
                if(progress > random.uniform()) bits |= bit;
            }
            word = bits;
        }
    }

    bool masked(tjs_int y) const {
        std::size_t word = (std::size_t)(y >> 6);
        return y >= 0 && word < lineMask.size() && ((lineMask[word] >> (y & 63)) & 1) != 0;
    }

    void renderRow(tjs_uint8 *out, const GlitchSource &source, double coef, tjs_int y, tjs_int head,
                   tjs_int column, tjs_int left, tjs_int width) const;
    void renderMasked(tjs_uint8 *dest, tjs_int destPitch, const GlitchSource &first,
                      const GlitchSource &second, double firstCoef, double secondCoef,
                      const GlitchRect &area) const;
};

// Copies count pixels from (x, y), splitting colour channels when shift is non-zero.
std::size_t GlitchSegment(tjs_uint8 *out, const GlitchSource &source, tjs_int x, tjs_int y, tjs_int shift,
                          tjs_int count) {
    std::ptrdiff_t row = (std::ptrdiff_t)y * source.pitch, bytes = 4 * (std::ptrdiff_t)count;
    const tjs_uint8 *center = source.clamped(row + 4 * (std::ptrdiff_t)x, bytes);
    if(!shift) {
        std::memmove(out, center, (std::size_t)bytes);
        return (std::size_t)bytes;
    }
    const tjs_uint8 *before = source.clamped(row + 4 * ((std::ptrdiff_t)x - 4 * shift), bytes);
    const tjs_uint8 *after = source.clamped(row + 4 * ((std::ptrdiff_t)x + 4 * shift), bytes);
    for(tjs_int i = 0; i < count; ++i, out += 4, before += 4, center += 4, after += 4) {
        switch(shift & 3) {
            case 0: out[0] = before[0]; out[1] = center[1]; out[2] = after[2]; break;
            case 1: out[0] = before[2]; out[1] = center[1]; out[2] = after[0]; break;
            case 2: out[0] = before[1]; out[1] = center[0]; out[2] = after[2]; break;
            default: out[0] = before[0]; out[1] = center[2]; out[2] = after[1]; break;
        }
        out[3] = center[3];
    }
    return (std::size_t)bytes;
}

void GlitchField::render(tjs_uint8 *dest, tjs_int destPitch, const GlitchSource &source, double coef,
                         const GlitchRect &area) const {
    tjs_int left = area.left, width = area.right - area.left;
    tjs_int column = left / size, head = left % size;
    for(tjs_int y = area.top, remainingRows = area.bottom - area.top; remainingRows > 0; --remainingRows, ++y) {
        tjs_uint8 *out = dest;
        tjs_int remaining = width, x = left;
        double line = at(lines, y);
        std::ptrdiff_t cell = column + (std::ptrdiff_t)columns * (y / size);
        auto segment = [&](tjs_int start, tjs_int count) {
            return GlitchSegment(out, source, start + (tjs_int)((line + at(blockX, cell)) * coef),
                                 y + (tjs_int)(at(blockY, cell) * coef), (tjs_int)(at(blockCol, cell) * coef), count);
        };
        if(head > 0) {
            out += segment(left, head);
            remaining = width - head;
            x = head + left;
            ++cell;
        }
        for(; remaining >= size; remaining -= size, x += size, ++cell) out += segment(x, size);
        if(remaining > 0) segment(x, remaining);
        dest += destPitch;
    }
}

void GlitchField::renderRow(tjs_uint8 *out, const GlitchSource &source, double coef, tjs_int y,
                            tjs_int head, tjs_int column, tjs_int left, tjs_int width) const {
    double line = at(lines, y);
    std::ptrdiff_t cell = column + (std::ptrdiff_t)columns * (y / size);
    tjs_int x = left, remaining = width;
    auto segment = [&](tjs_int start, tjs_int count) {
        return GlitchSegment(out, source, start + (tjs_int)((at(blockX, cell) + line) * coef),
                             y + (tjs_int)(at(blockY, cell) * coef), (tjs_int)(at(blockCol, cell) * coef), count);
    };
    if(head > 0) {
        out += segment(left, head);
        x = head + left;
        remaining = width - head;
        ++cell;
    }
    for(; remaining >= size; remaining -= size, x += size, ++cell) out += segment(x, size);
    if(remaining > 0) segment(x, remaining);
}

void GlitchField::renderMasked(tjs_uint8 *dest, tjs_int destPitch, const GlitchSource &first,
                               const GlitchSource &second, double firstCoef, double secondCoef,
                               const GlitchRect &area) const {
    tjs_int left = area.left, width = area.right - area.left;
    tjs_int column = left / size, head = left % size;
    for(tjs_int y = area.top, rows = area.bottom - area.top; rows > 0; --rows, ++y, dest += destPitch) {
        bool useSecond = masked(y);
        renderRow(dest, useSecond ? second : first, useSecond ? secondCoef : firstCoef, y, head, column, left,
                  width);
    }
}

tjs_error GlitchCopy::apply(tTJSVariantClosure &elm) {
    tjs_int width = 0, height = 0, pitch = 0;
    if(!GlitchImageSize(dest, width, height) || !GlitchPitch(dest, pitch, true)) return TJS_E_FAIL;
    auto *buffer = (tjs_uint8 *)(tjs_intptr_t)GlitchLayerInteger(dest, TJS_W("mainImageBufferForWrite"), 0);
    if(!buffer) return TJS_E_FAIL;
    tjs_uint8 *target = buffer + (std::ptrdiff_t)rect.top * pitch + 4 * rect.left;
    GlitchDictionary dictionary{elm};
    GlitchParams params = GlitchReadParams(dictionary);
    tjs_int size = dictionary.integer(TJS_W("size"), 16);
    if(size <= 0) size = 16;
    GlitchField field(params, rect.right - rect.left, rect.bottom - rect.top, size);
    tTJSVariant seedValue;
    std::uint64_t seed = dictionary.get(TJS_W("seed"), seedValue) ? (std::uint64_t)seedValue.AsInteger()
                                                                   : TVPGetTickCount();
    field.random.s0 = GlitchSplitMix(seed);
    field.random.s1 = GlitchSplitMix(seed);
    field.fillLines();
    field.fillBlocks();
    double coef = dictionary.real(TJS_W("coef"), 1.0);
    field.render(target, pitch, source, coef, rect);
    return TJS_S_OK;
}

tjs_error GlitchRun(tTJSVariant *result, iTJSDispatch2 *objthis, tTJSVariant *image, tTJSVariant *elm,
                    const tjs_int *offsets, const tjs_int *w, const tjs_int *h) {
    if(image->Type() != tvtObject || elm->Type() != tvtObject) return TJS_E_INVALIDPARAM;
    iTJSDispatch2 *src = image->AsObjectNoAddRef();
    if(!objthis || !src) return TJS_E_INVALIDPARAM;
    if(result) result->Clear();
    GlitchCopy copy(objthis, src);
    tjs_error status = copy.prepare(offsets[0], offsets[1], offsets[2], offsets[3], w, h);
    if(status == 1) return TJS_S_OK;
    if(status >= 0) status = copy.apply(elm->AsObjectClosureNoAddRef());
    return status;
}


//---------------------------------------------------------------------------
// Transitions registered by the same plugin: glitch, fadeglitch, loopglitch.
//---------------------------------------------------------------------------

// Options are read with GetValue; a failed or void value keeps the default.
struct GlitchOptions {
    iTVPSimpleOptionProvider *options;

    bool get(const tjs_char *name, tTJSVariant &value) const {
        return options && TJS_SUCCEEDED(options->GetValue(name, &value)) && value.Type() != tvtVoid;
    }
    double real(const tjs_char *name, double fallback) const {
        tTJSVariant value;
        return get(name, value) ? (double)value : fallback;
    }
    tjs_int integer(const tjs_char *name, tjs_int fallback) const {
        tTJSVariant value;
        return get(name, value) ? (tjs_int)value : fallback;
    }
    tTVInteger integer64(const tjs_char *name, tTVInteger fallback) const {
        tTJSVariant value;
        return get(name, value) ? (tTVInteger)value : fallback;
    }
    tjs_int block() const {
        tjs_int size = integer(TJS_W("block"), 16);
        return size <= 0 ? 16 : size;
    }
    // A negative time rejects the transition; shorter times become 2 ms.
    bool time(tjs_int64 &value) const {
        tjs_int ms = integer(TJS_W("time"), -1);
        if(ms < 0) return false;
        value = ms < 2 ? 2 : ms;
        return true;
    }
};

// The source of a transition image: rows from its top-left, bounded by the
// provider's full size.
bool GlitchImageSource(iTVPScanLineProvider *provider, GlitchSource &source) {
    iTVPTexture2D *texture = provider->GetTexture();
    auto *origin = (tjs_uint8 *)texture->GetScanLineForRead(0);
    if(!origin) return false;
    tjs_int width = 0, height = 0;
    if(TJS_FAILED(provider->GetWidth(&width)) || TJS_FAILED(provider->GetHeight(&height))) width = height = 0;
    source.set(origin, texture->GetPitch(), width, height);
    return true;
}

class GlitchTransBase : public iTVPDivisibleTransHandler {
    tjs_int refCount = 1;

protected:
    GlitchField field;
    double progress = -1.0; // negative until the first frame
    tjs_uint64 startTick = 0;
    tjs_int64 time;

    virtual void update(tjs_uint64 tick) = 0;
    virtual void draw(tjs_uint8 *dest, tjs_int destPitch, const GlitchSource &first,
                      const GlitchSource &second, const GlitchRect &area) = 0;

public:
    GlitchTransBase(const GlitchParams &params, tjs_int width, tjs_int height, tjs_int block, tjs_int64 ms)
        : field(params, width, height, block), time(ms) {}
    virtual ~GlitchTransBase() = default;

    tjs_error AddRef() override {
        ++refCount;
        return TJS_S_OK;
    }
    tjs_error Release() override {
        if(refCount == 1) delete this;
        else --refCount;
        return TJS_S_OK;
    }
    tjs_error SetOption(iTVPSimpleOptionProvider *) override { return TJS_S_OK; }

    // The elapsed time is compared as a signed 64-bit value.
    tjs_error StartProcess(tjs_uint64 tick) override {
        if(progress < 0.0) startTick = tick;
        tjs_int64 elapsed = (tjs_int64)(tick - startTick);
        if(elapsed >= time) return TJS_S_FALSE;
        progress = (double)elapsed / (double)time;
        update(tick);
        return TJS_S_TRUE;
    }
    tjs_error EndProcess() override { return TJS_S_TRUE; }

    // Sources are addressed from their top-left; the destination from its
    // rectangle. The plugin dereferences a missing second source, which a
    // simple transition without a source layer does not provide; that case
    // fails here instead.
    tjs_error Process(tTVPDivisibleData *data) override {
        if(!data->Dest || !data->Src1 || !data->Src2) return TJS_E_FAIL;
        iTVPTexture2D *target = data->Dest->GetTextureForRender();
        auto *dest = (tjs_uint8 *)target->GetScanLineForWrite(data->DestTop);
        GlitchSource first, second;
        if(!dest || !GlitchImageSource(data->Src1, first) || !GlitchImageSource(data->Src2, second))
            return TJS_E_FAIL;
        dest += 4 * (std::ptrdiff_t)data->DestLeft;
        GlitchRect area{data->Left, data->Top, data->Left + data->Width, data->Top + data->Height};
        draw(dest, target->GetPitch(), first, second, area);
        return TJS_S_OK;
    }

    tjs_error MakeFinalImage(iTVPScanLineProvider **dest, iTVPScanLineProvider *, iTVPScanLineProvider *src2) override {
        *dest = src2;
        return TJS_S_OK;
    }
};

// glitch: rows switch from the first image to the second as the transition
// advances; each image is displaced by its own gamma-shaped amount.
class TransGlitch : public GlitchTransBase {
    double gammaIn, gammaOut;

    void update(tjs_uint64 tick) override {
        double current = progress;
        field.reseed(tick);
        field.fillLines();
        field.fillBlocks();
        field.fillMask(current);
    }
    void draw(tjs_uint8 *dest, tjs_int destPitch, const GlitchSource &first, const GlitchSource &second,
              const GlitchRect &area) override {
        double secondCoef = std::pow(1.0 - progress, gammaOut);
        double firstCoef = std::pow(progress, gammaIn);
        field.renderMasked(dest, destPitch, first, second, firstCoef, secondCoef, area);
    }

public:
    TransGlitch(const GlitchParams &params, tjs_int width, tjs_int height, tjs_int block, tjs_int64 ms,
                double in, double out)
        : GlitchTransBase(params, width, height, block, ms), gammaIn(in), gammaOut(out) {}

    static iTVPBaseTransHandler *create(const GlitchOptions &options, tjs_uint width, tjs_uint height) {
        tjs_int64 ms;
        if(!options.time(ms)) return nullptr;
        GlitchParams params = GlitchReadParams(options);
        double in = options.real(TJS_W("gamma_in"), 1.0);
        double out = options.real(TJS_W("gamma_out"), 1.0);
        return new TransGlitch(params, (tjs_int)width, (tjs_int)height, options.block(), ms, in, out);
    }
};

// Overlays a colour on the destination: channels at 128 keep it, lower
// darken and higher brighten.
void GlitchColorBlend(tjs_uint8 *dest, tjs_int pitch, tjs_uint32 color, const GlitchRect &area) {
    tjs_uint32 high = (((color >> 7) & 0x10101) + 0x7F7F7F) ^ 0x7F7F7F;
    tjs_uint32 low = color & 0x7F7F7F;
    for(tjs_int rows = area.bottom - area.top; rows > 0; --rows, dest += pitch) {
        auto *pixel = (tjs_uint32 *)dest;
        for(tjs_int count = area.right - area.left; count > 0; --count, ++pixel) {
            tjs_uint32 carry = (low + ((*pixel >> 1) & 0x7F7F7F)) & 0x808080;
            tjs_uint32 mask = ((carry >> 7) + 0x7F7F7F) ^ 0x7F7F7F;
            *pixel = (*pixel & 0xFF000000) | (high & mask) | ((high | mask) & (*pixel + 2 * (low - carry)));
        }
    }
}

const tjs_uint32 GlitchNeutralColor = 0x808080;

// fadeglitch: one image is displaced while a colour fades in over it.
class FadeGlitch : public GlitchTransBase {
    double gamma;
    tjs_uint32 color, current = GlitchNeutralColor;
    bool colored, fadeIn;

    double amount() const { return fadeIn ? 1.0 - progress : progress; }

    // Each channel moves from 128 toward the colour by weight.
    tjs_uint32 mix(double weight) const {
        auto channel = [&](int shift) {
            return (tjs_uint32)(tjs_int64)((double)((color >> shift) & 0xFF) * weight + (1.0 - weight) * 128.0);
        };
        return channel(0) | ((channel(8) | (channel(16) << 8)) << 8);
    }
    void update(tjs_uint64 tick) override {
        double value = amount();
        current = mix(value * value);
        field.reseed(tick);
        field.fillLines();
        field.fillBlocks();
    }
    void draw(tjs_uint8 *dest, tjs_int destPitch, const GlitchSource &first, const GlitchSource &second,
              const GlitchRect &area) override {
        double coef = std::pow(amount(), gamma);
        field.render(dest, destPitch, fadeIn ? second : first, coef, area);
        if(colored) GlitchColorBlend(dest, destPitch, current, area);
    }

public:
    FadeGlitch(const GlitchParams &params, tjs_int width, tjs_int height, tjs_int block, tjs_int64 ms,
               double power, tjs_uint32 target, bool in)
        : GlitchTransBase(params, width, height, block, ms), gamma(power), color(target),
          colored(target != GlitchNeutralColor), fadeIn(in) {}

    static iTVPBaseTransHandler *create(const GlitchOptions &options, tjs_uint width, tjs_uint height) {
        tjs_int64 ms;
        if(!options.time(ms)) return nullptr;
        GlitchParams params = GlitchReadParams(options);
        bool noFade = options.integer(TJS_W("nofade"), 0) != 0;
        bool in = options.integer(TJS_W("fadein"), 0) != 0;
        double power = options.real(in ? TJS_W("gamma_in") : TJS_W("gamma_out"), 1.0);
        tjs_int block = options.block();
        tjs_uint32 target = noFade ? GlitchNeutralColor : (tjs_uint32)options.integer64(TJS_W("color"), 0);
        return new FadeGlitch(params, (tjs_int)width, (tjs_int)height, block, ms, power, target, in);
    }
};

// loopglitch: displaces the first image by "coef" read from the options held
// by SetOption. Its time is -1, so under the signed comparison its first
// frame already reports completion; this follows the plugin.
class LoopGlitch : public GlitchTransBase {
    iTVPSimpleOptionProvider *options = nullptr;
    double coef = 1.0;
    bool stop = false;

    void update(tjs_uint64 tick) override {
        if(options) {
            tTJSVariant value;
            if(TJS_SUCCEEDED(options->GetValue(TJS_W("coef"), &value))) coef = value.AsReal();
            if(TJS_SUCCEEDED(options->GetValue(TJS_W("break"), &value))) stop = (bool)value;
        }
        field.reseed(tick);
        field.fillLines();
        field.fillBlocks();
    }
    void draw(tjs_uint8 *dest, tjs_int destPitch, const GlitchSource &first, const GlitchSource &,
              const GlitchRect &area) override {
        field.render(dest, destPitch, first, coef, area);
    }

public:
    LoopGlitch(const GlitchParams &params, tjs_int width, tjs_int height, tjs_int block)
        : GlitchTransBase(params, width, height, block, -1) {}

    tjs_error SetOption(iTVPSimpleOptionProvider *provider) override {
        options = provider;
        return TJS_S_OK;
    }

    static iTVPBaseTransHandler *create(const GlitchOptions &options, tjs_uint width, tjs_uint height) {
        GlitchParams params = GlitchReadParams(options);
        return new LoopGlitch(params, (tjs_int)width, (tjs_int)height, options.block());
    }
};

template <class Handler> class GlitchTransProvider : public iTVPTransHandlerProvider {
    tjs_uint refCount = 1;
    const tjs_char *name;
    tTVPTransType type;
    bool sameSize;

public:
    GlitchTransProvider(const tjs_char *transition, tTVPTransType kind, bool same)
        : name(transition), type(kind), sameSize(same) {}

    tjs_error AddRef() override {
        ++refCount;
        return TJS_S_OK;
    }
    tjs_error Release() override {
        if(refCount == 1) delete this;
        else --refCount;
        return TJS_S_OK;
    }
    tjs_error GetName(const tjs_char **out) override {
        if(out) *out = name;
        return TJS_S_OK;
    }
    tjs_error StartTransition(iTVPSimpleOptionProvider *options, iTVPSimpleImageProvider *, tTVPLayerType,
                              tjs_uint src1w, tjs_uint src1h, tjs_uint src2w, tjs_uint src2h,
                              tTVPTransType *kind, tTVPTransUpdateType *updatetype,
                              iTVPBaseTransHandler **handler) override {
        if(!handler || !options || (sameSize && (src1w != src2w || src1h != src2h))) return TJS_E_FAIL;
        if(kind) *kind = type;
        if(updatetype) *updatetype = tutDivisibleFade;
        *handler = Handler::create(GlitchOptions{options}, src1w, src1h);
        return *handler ? TJS_S_OK : TJS_E_FAIL;
    }
};

iTVPTransHandlerProvider *GlitchTransition = nullptr, *GlitchFadeTransition = nullptr,
                         *GlitchLoopTransition = nullptr;

template <class Handler>
void GlitchAddTransition(iTVPTransHandlerProvider *&slot, const tjs_char *name, tTVPTransType type, bool same) {
    if(slot) return;
    slot = new GlitchTransProvider<Handler>(name, type, same);
    TVPAddTransHandlerProvider(slot);
}

void GlitchRemoveTransition(iTVPTransHandlerProvider *&slot) {
    if(!slot) return;
    TVPRemoveTransHandlerProvider(slot);
    slot->Release();
    slot = nullptr;
}

} // namespace

// Layer.glitchCopy(dx, dy, src, sx, sy, w, h, elm)
static tjs_error GlitchCopyMethod(tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
                                                  iTJSDispatch2 *objthis) {
    if(numparams < 8) return TJS_E_BADPARAMCOUNT;
    tjs_int offsets[4] = {(tjs_int)*param[0], (tjs_int)*param[1], (tjs_int)*param[3], (tjs_int)*param[4]};
    tjs_int w = (tjs_int)*param[5], h = (tjs_int)*param[6];
    return GlitchRun(result, objthis, param[2], param[7], offsets, &w, &h);
}

// Layer.doGlitch(src, elm) applies the effect over the whole source image.
static tjs_error DoGlitchMethod(tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
                                                iTJSDispatch2 *objthis) {
    if(numparams < 2) return TJS_E_BADPARAMCOUNT;
    const tjs_int offsets[4] = {0, 0, 0, 0};
    return GlitchRun(result, objthis, param[0], param[1], offsets, nullptr, nullptr);
}

NCB_ATTACH_FUNCTION(glitchCopy, Layer, GlitchCopyMethod);
NCB_ATTACH_FUNCTION(doGlitch, Layer, DoGlitchMethod);

// Linking registers the transitions before the Layer methods; unlinking
// removes them first.
static void GlitchRegisterTransitions() {
    GlitchAddTransition<TransGlitch>(GlitchTransition, TJS_W("glitch"), ttExchange, true);
    GlitchAddTransition<FadeGlitch>(GlitchFadeTransition, TJS_W("fadeglitch"), ttSimple, false);
    GlitchAddTransition<LoopGlitch>(GlitchLoopTransition, TJS_W("loopglitch"), ttSimple, false);
}

static void GlitchUnregisterTransitions() {
    GlitchRemoveTransition(GlitchTransition);
    GlitchRemoveTransition(GlitchFadeTransition);
    GlitchRemoveTransition(GlitchLoopTransition);
}

NCB_PRE_REGIST_CALLBACK(GlitchRegisterTransitions);
NCB_PRE_UNREGIST_CALLBACK(GlitchUnregisterTransitions);
