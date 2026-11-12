#pragma once

#include "LayerExBase.h"
#include "motionplayer/EmoteBlinkRng.h"
#include <cstdint>
#include <cstring>
#include <vector>

// Owned or borrowed row storage, including the original signed image pitch.
template<class T> struct GfxArray {
    int Width = 0, Height = 0;
    T *Data = nullptr;
    T **Rows = nullptr;
    bool Owned = false;

    ~GfxArray() { clear(); }
    void clear() {
        if(Owned && Data) delete[] Data;
        if(Rows) delete[] Rows;
        Width = Height = 0; Data = nullptr; Rows = nullptr; Owned = false;
    }
    void resize(int width, int height) {
        if(!Data || !Owned || Width != width || Height != height) {
            clear(); Width = width; Height = height;
            Data = new T[width * height];
            Rows = new T *[height]; Owned = true;
            for(int y = 0; y < height; ++y) Rows[y] = Data + y * width;
        }
        std::memset(Data, 0, sizeof(T) * Width * Height);
    }
    int pitch() const {
        return Height > 1 ? int(reinterpret_cast<char *>(Rows[1]) -
                               reinterpret_cast<char *>(Rows[0])) : Width * int(sizeof(T));
    }
    void borrow(T *data, int width, int height, int stride) {
        if(Data != data || Width != width || Height != height || pitch() != stride) {
            clear(); Width = width; Height = height; Data = data;
            Rows = new T *[height]; Owned = false;
            for(int y = 0; y < height; ++y)
                Rows[y] = reinterpret_cast<T *>(reinterpret_cast<char *>(data) + y * stride);
        }
    }
};

struct GfxLayer : NI_LayerExBase {
    iTJSDispatch2 *Object;
    int Left, Top, ImageLeft, ImageTop;
    void bind(iTJSDispatch2 *object);
};

class gfxFire : public GfxLayer {
    struct Seed : GfxArray<std::uint8_t> { int X, Y; };
    struct Point { float X, Y; };
    struct Rect { int Left, Top, Right, Bottom; };

    GfxLayer SeedLayer;
    GfxArray<std::uint32_t> Pixels;
    GfxArray<std::uint8_t> Cool, Heat, Work;
    std::vector<Seed *> Seeds;
    GfxArray<float> GridX, GridY, VelocityX, VelocityY;
    const std::uint32_t *Palette;
    int ForceH = -1, ForceV = 0, BoundH = 8, BoundV = 4;
    int Blur = 20, CoolRange = 1, CoolStrength = 160;
    int DensityDenominator = 320, DensityNumerator = 35, RandomSeed = 0;
    int BlockCount = 0, Filter = 0;
    float **Blocks = nullptr, *LeftBoundary = nullptr, *RightBoundary = nullptr;
    float Scaling = 0.00003f;
    bool EdgeSmoothing = false;
    std::vector<int> EdgeCounts;
    motion::EmoteBlinkMt19937 *Random;

    void allocate();
    void allocateSpans();
    void copySeed(int index);
    void addSeeds();
    void advanceGrid();
    void buildSpans();
    void sample(bool filtered);
    void coolAndColor();
    void boundary(const Point &source0, const Point &dest0,
                  const Point &dest1, const Point &source1, bool right);
    void edge(const Point &source0, const Point &dest0,
              const Point &dest1, const Point &source1);
    float *span(int row, int index) { return Blocks[row / 8] + (row & 7) + 24 * index; }
    void sortSpans(int row, int count);
    std::uint8_t sampleFiltered(int u, int v, int stride, unsigned size) const;
    static void textureSpan(float &left, float &right, float &u0, float u1,
                            float &v0, float v1, int width,
                            int &u, int &v, int &du, int &dv);
    static bool clip(Rect &dest, Rect &source, int dw, int dh, int sw, int sh);
    double random() { return motion::EmoteBlinkRng_nextUnit32_guess(Random); }

public:
    gfxFire();
    // Raw callback argument counts and defaults are part of the native API.
    static tjs_error init(tTJSVariant *, tjs_int, tTJSVariant **, gfxFire *);
    static tjs_error clearFireSeed(tTJSVariant *, tjs_int, tTJSVariant **, gfxFire *);
    static tjs_error setFireSeed(tTJSVariant *, tjs_int, tTJSVariant **, gfxFire *);
    static tjs_error setFireSeedPos(tTJSVariant *, tjs_int, tTJSVariant **, gfxFire *);
    void updateWarpMap();
    void updateCoolMap();
    void cycle();
    void applyRed(); void applyBlue(); void applyWhite(); void applyCustom();
    void setCustomColorTable(iTJSDispatch2 *layer);
    bool isRed() const; bool isBlue() const; bool isWhite() const;
    void setSeedLayer(iTJSDispatch2 *layer) { SeedLayer.bind(layer); copySeed(0); }
    void setTargetLayer(iTJSDispatch2 *layer) { bind(layer); allocate(); }
    int getSeedX() const { return Seeds[0]->X; }
    int getSeedY() const { return Seeds[0]->Y; }
    void setSeedX(int value) { Seeds[0]->X = value; }
    void setSeedY(int value) { Seeds[0]->Y = value; }
    int getNumOfSeeds() const { return int(Seeds.size()); }
    int getRandomSeed() const { return RandomSeed; }
    void setRandomSeed(int value) { RandomSeed = value; }
    int getForceH() const { return ForceH; }
    int getForceV() const { return ForceV; }
    void setForceH(int value) { if(value >= -7 && value <= 7) ForceH = value; }
    void setForceV(int value) { if(value >= -7 && value <= 7) ForceV = value; }
    int getBoundRangeH() const { return BoundH; }
    int getBoundRangeV() const { return BoundV; }
    void setBoundRangeH(int value) { if(value >= -7 && value <= 8) BoundH = value; }
    void setBoundRangeV(int value) { if(value >= -7 && value <= 8) BoundV = value; }
    float getScalingCoeff() const { return Scaling; }
    void setScalingCoeff(float value) { if(value > 0.0f && value < 0.01f) Scaling = value; }
    int getNumOfBlurForCoolMap() const { return Blur; }
    void setNumOfBlurForCoolMap(int value) { if(value > 0) Blur = value; }
    int getTextureFilterType() const { return Filter; }
    void setTextureFilterType(int value) { Filter = value != 0; }
    int getCoolRange() const { return CoolRange; }
    int getCoolStrength() const { return CoolStrength; }
    void setCoolRange(int value) { if(value >= 0 && value <= 255) CoolRange = value; }
    void setCoolStrength(int value) { if(value >= 0 && value <= 255) CoolStrength = value; }
    int getCoolParticleDensityDenominator() const { return DensityDenominator; }
    int getCoolParticleDensityNumerator() const { return DensityNumerator; }
    void setCoolParticleDensityDenominator(int value) { if(value > 0) DensityDenominator = value; }
    void setCoolParticleDensityNumerator(int value) { if(value > 0) DensityNumerator = value; }
    bool getEdgeSmoothing() const { return EdgeSmoothing; }
    void setEdgeSmoothing(bool value) { EdgeSmoothing = value; }
};
