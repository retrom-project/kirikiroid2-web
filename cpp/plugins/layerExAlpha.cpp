#define NCB_MODULE_NAME TJS_W("layerExAlpha.dll")
#include "ncbind.hpp"
#include "layerExBase.hpp"
#include "DebugIntf.h"
#include <vector>

struct layerExAlpha : layerExBase_GL {
    explicit layerExAlpha(DispatchT obj) : layerExBase_GL(obj) {}

    void turnAlpha() {
        if(TVPIsSoftwareRenderManager()) {
            auto *row = _buffer;
            for(int y = 0; y < _height; ++y) {
                auto *pixel = reinterpret_cast<tjs_uint32 *>(row);
                for(int x = 0; x < _width; ++x) {
                    pixel[x] = ~pixel[x] & 0xff000000u;
                }
                row += _pitch;
            }
        } else {
            static iTVPRenderMethod *method = TVPGetRenderManager()
                ->CompileRenderMethod("turnAlpha",
                    "void main() { gl_FragColor = vec4(0,0,0,1); }\n", 0, 0)
                ->SetBlendFuncSeparate(GL_FUNC_ADD, GL_ZERO, GL_ZERO,
                                       GL_ONE_MINUS_DST_ALPHA, GL_ZERO);
            auto *texture = _this->GetMainImage()->GetTextureForRender(true, nullptr);
            TVPGetRenderManager()->OperateRect(
                method, texture, nullptr,
                tTVPRect(_clipLeft, _clipTop, _clipLeft + _clipWidth,
                         _clipTop + _clipHeight), tRenderTexRectArray());
        }
        redraw();
    }

    void turnAlpha2() {
        if(TVPIsSoftwareRenderManager()) {
            auto *row = _buffer;
            for(int y = 0; y < _height; ++y) {
                auto *pixel = reinterpret_cast<tjs_uint32 *>(row);
                for(int x = 0; x < _width; ++x) {
                    pixel[x] ^= 0xff000000u;
                }
                row += _pitch;
            }
        } else {
            static iTVPRenderMethod *method = TVPGetRenderManager()
                ->CompileRenderMethod("turnAlpha2",
                    "void main() { gl_FragColor = vec4(0,0,0,1); }\n", 0, 0)
                ->SetBlendFuncSeparate(GL_FUNC_ADD, GL_ZERO, GL_ONE,
                                       GL_ONE_MINUS_DST_ALPHA, GL_ZERO);
            auto *texture = _this->GetMainImage()->GetTextureForRender(true, nullptr);
            TVPGetRenderManager()->OperateRect(
                method, texture, nullptr,
                tTVPRect(_clipLeft, _clipTop, _clipLeft + _clipWidth,
                         _clipTop + _clipHeight), tRenderTexRectArray());
        }
        redraw();
    }

    void turnAlpha3() {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            auto *pixel = row;
            for(int x = 0; x < _width; ++x, pixel += 4) {
                if(pixel[3]) pixel[3] = ~pixel[3];
            }
            row += _pitch;
        }
        redraw();
    }

    void turnOutline() {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            auto *pixel = row;
            for(int x = 0; x < _width; ++x, pixel += 4) {
                auto *value = reinterpret_cast<tjs_uint32 *>(pixel);
                *value = pixel[3] == 255 ? 0 : *value & 0xff000000u;
            }
            row += _pitch;
        }
        redraw();
    }

    void AlphaColorRect(int red, int green, int blue) {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            auto *pixel = row;
            for(int x = 0; x < _width; ++x, pixel += 4) {
                if(blue != -1) pixel[0] = blue;
                if(green != -1) pixel[1] = green;
                if(red != -1) pixel[2] = red;
            }
            row += _pitch;
        }
        redraw();
    }

    void BlackForAlpha() {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            auto *pixel = row;
            for(int x = 0; x < _width; ++x, pixel += 4) {
                pixel[3] = (pixel[0] + pixel[1] + pixel[2]) / 3;
                pixel[0] = pixel[1] = 0;
                pixel[2] = 0;
            }
            row += _pitch;
        }
        redraw();
    }

    void AlphaAdd2(int amount, int level) {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            auto *pixel = row;
            for(int x = 0; x < _width; ++x, pixel += 4) {
                int alpha = pixel[3] + amount;
                pixel[3] = alpha < level || alpha < 0 ? 0 : alpha >= 255 ? 255 : alpha;
            }
            row += _pitch;
        }
        redraw();
    }

    void AlphaBlend(int amount) {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            auto *pixel = row;
            for(int x = 0; x < _width; ++x, pixel += 4) {
                pixel[3] = static_cast<int>((static_cast<float>(amount) / 255.0f) * pixel[3]);
            }
            row += _pitch;
        }
        redraw();
    }

    void ColorAlphaAdd(int red, int green, int blue, int amount) {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            auto *pixel = row;
            for(int x = 0; x < _width; ++x, pixel += 4) {
                int channel = pixel[0] + amount;
                pixel[0] = channel > blue ? blue : channel;
                channel = pixel[1] + amount;
                pixel[1] = channel > green ? green : channel;
                channel = pixel[2] + amount;
                pixel[2] = channel > red ? red : channel;
                int alpha = pixel[3] - amount;
                pixel[3] = alpha < 0 ? 0 : alpha >= 255 ? 255 : alpha;
            }
            row += _pitch;
        }
        redraw();
    }

    void AlphaDivide(int level) {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            const unsigned char alpha = row[3] < level ? 0 : 255;
            for(int x = 0; x < _width; ++x) row[x * 4 + 3] = alpha;
            row += _pitch;
        }
        redraw();
    }

    void AlphaAdd(int amount, bool nonTransparentOnly) {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        if(nonTransparentOnly) {
            auto *row = _buffer;
            for(int y = 0; y < _height; ++y) {
                auto *pixel = row;
                for(int x = 0; x < _width; ++x, pixel += 4) {
                    if(pixel[3]) {
                        int alpha = pixel[3] + amount;
                        pixel[3] = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
                    }
                }
                row += _pitch;
            }
        } else {
            auto *row = _buffer;
            for(int y = 0; y < _height; ++y) {
                auto *pixel = row;
                for(int x = 0; x < _width; ++x, pixel += 4) {
                    int alpha = pixel[3] + amount;
                    pixel[3] = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
                }
                row += _pitch;
            }
        }
        redraw();
    }

    void ColorVectorEffectAdd(int r1, int r2, int g1, int g2, int b1, int b2, int amount) {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            auto *pixel = row;
            for(int x = 0; x < _width; ++x, pixel += 4) {
                if(pixel[3]) {
                    int alpha = pixel[3] - amount;
                    pixel[3] = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
                    int index = (255 - pixel[3]) / amount;
                    int red = r1 + (r2 - r1) / (255 / amount) * index;
                    int green = g1 + (g2 - g1) / (255 / amount) * index;
                    int blue = b1 + (b2 - b1) / (255 / amount) * index;
                    pixel[2] = red < 0 ? 0 : red > 255 ? 255 : red;
                    pixel[1] = green < 0 ? 0 : green > 255 ? 255 : green;
                    pixel[0] = blue < 0 ? 0 : blue > 255 ? 255 : blue;
                }
            }
            row += _pitch;
        }
        redraw();
    }

    void ExtraColorVectorEffectAdd(int r1, int r2, int g1, int g2, int b1, int b2, int amount) {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            auto *pixel = row;
            for(int x = 0; x < _width; ++x, pixel += 4) {
                if(pixel[3]) {
                    int alpha = pixel[3] - amount;
                    pixel[3] = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
                    int index = (255 - pixel[3]) / amount;
                    unsigned int blue = pixel[0], green = pixel[1], red = pixel[2];
                    unsigned int average = (blue + green + red) / 3;
                    if(index && static_cast<int>(average) > (r2 + g2 + b2) / 3) {
                        int rv = r1 + (r2 - r1) / (255 / amount) * index;
                        int gv = g1 + (g2 - g1) / (255 / amount) * index;
                        int bv = b1 + (b2 - b1) / (255 / amount) * index;
                        pixel[2] = rv < 0 ? 0 : rv > 255 ? 255 : rv;
                        pixel[1] = gv < 0 ? 0 : gv > 255 ? 255 : gv;
                        pixel[0] = bv < 0 ? 0 : bv > 255 ? 255 : bv;
                        if(pixel[2] <= average) pixel[2] = rv < 0 ? 0 : rv > 255 ? 255 : rv;
                        if(pixel[1] <= average) pixel[1] = gv < 0 ? 0 : gv > 255 ? 255 : gv;
                        if(pixel[0] <= average) pixel[0] = bv < 0 ? 0 : bv > 255 ? 255 : bv;
                    } else {
                        if(blue <= average) pixel[0] = blue = static_cast<unsigned char>(r1);
                        if(green <= average) pixel[1] = green = static_cast<unsigned char>(g1);
                        if(red <= average) pixel[2] = red = static_cast<unsigned char>(b1);
                        int selected = green < red ? 1 : 2;
                        if(blue > green) selected = 2 * (blue <= red);
                        int channel = pixel[selected];
                        channel = static_cast<unsigned int>(channel) <= average ? channel + amount : channel - amount;
                        pixel[selected] = channel > static_cast<int>(average) ? channel : average;
                    }
                }
            }
            row += _pitch;
        }
        redraw();
    }

    void WhiteToBlueBlurAdd(int amount) {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        const float rate = (static_cast<float>(255 - amount) / 255.0f) * 0.9f;
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            auto *pixel = row;
            for(int x = 0; x < _width; ++x, pixel += 4) {
                const int alpha = pixel[3];
                if(alpha) {
                    unsigned int blue = pixel[0], green = pixel[1], red = pixel[2];
                    unsigned int sum = blue + green + red;
                    if(sum > 590) {
                        pixel[0] = 255;
                        if(alpha == 255) pixel[1] = pixel[2] = 255;
                        else {
                            pixel[1] = static_cast<int>(rate * green);
                            pixel[2] = static_cast<int>(rate * red);
                        }
                    } else if(alpha == 255 && blue < sum / 3) {
                        pixel[0] = 255;
                        pixel[1] = pixel[2] = sum / 3;
                    }
                    int value = alpha - amount;
                    pixel[3] = value < 0 ? 0 : value > 255 ? 255 : value;
                }
            }
            row += _pitch;
        }
        redraw();
    }

    void ExMaskRect(int dx, int dy, iTJSDispatch2 *sourceObject,
                 int sx, int sy, int width, int height, int type) {
        (void)sx;
        const int destinationWidth = _width, destinationHeight = _height;
        if(destinationWidth < dx || destinationHeight < dy) {
            if(destinationWidth <= dx)
                TVPAddLog(TJS_W("layerExAlpha: Src is position for OutArea:[Left]"));
            if(_height <= dy)
                TVPAddLog(TJS_W("layerExAlpha: Src is position for OutArea:[Top]"));
            return;
        }
        tTJSNI_Layer *source;
        if(TJS_FAILED(sourceObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&source))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int left = dx < 0 ? 0 : dx, top = dy < 0 ? 0 : dy;
        int copyWidth = width + (dx < 0 ? dx : 0);
        int copyHeight = height + (dy < 0 ? dy : 0);
        if(copyWidth + left > destinationWidth) copyWidth = destinationWidth - left;
        if(copyHeight + top > destinationHeight) copyHeight = destinationHeight - top;
        if(dy < 0) sy = -dy;
        const int sourcePitch = source->GetMainImagePixelBufferPitch();
        auto *sourceBuffer = static_cast<BufferT>(source->GetMainImagePixelBufferForWrite());
        auto *destinationAlpha = _buffer + 3;
        for(int y = 0; y < top; ++y) {
            for(int x = 0; x < _width; ++x) destinationAlpha[x * 4] = 0;
            destinationAlpha += _pitch;
        }
        auto *sourceAlpha = sourceBuffer + sourcePitch * sy + left * 4 + 3;
        const int bottom = top + copyHeight;
        switch(type) {
            case 0:
                for(int y = top; y < bottom; ++y) {
                    auto *pixel = destinationAlpha;
                    for(int x = 0; x < left; ++x, pixel += 4) *pixel = 0;
                    for(int x = 0; x < copyWidth; ++x) {
                        pixel[x * 4] = sourceAlpha[x * 4];
                    }
                    if(copyWidth > 0) pixel += copyWidth * 4;
                    for(int x = left + copyWidth; x < _width; ++x, pixel += 4) *pixel = 0;
                    sourceAlpha += sourcePitch;
                    destinationAlpha += _pitch;
                }
                break;
            case 1:
                for(int y = top; y < bottom; ++y) {
                    auto *pixel = destinationAlpha;
                    for(int x = 0; x < left; ++x, pixel += 4) *pixel = 0;
                    for(int x = 0; x < copyWidth; ++x) {
                        int alpha = pixel[x * 4] + sourceAlpha[x * 4];
                        pixel[x * 4] = alpha >= 255 ? 255 : alpha;
                    }
                    if(copyWidth > 0) pixel += copyWidth * 4;
                    for(int x = left + copyWidth; x < _width; ++x, pixel += 4) *pixel = 0;
                    sourceAlpha += sourcePitch;
                    destinationAlpha += _pitch;
                }
                break;
            case 2:
                for(int y = top; y < bottom; ++y) {
                    auto *pixel = destinationAlpha;
                    for(int x = 0; x < left; ++x, pixel += 4) *pixel = 0;
                    for(int x = 0; x < copyWidth; ++x) {
                        int alpha = pixel[x * 4] + sourceAlpha[x * 4] - 255;
                        pixel[x * 4] = alpha < 0 ? 0 : alpha;
                    }
                    if(copyWidth > 0) pixel += copyWidth * 4;
                    for(int x = left + copyWidth; x < _width; ++x, pixel += 4) *pixel = 0;
                    sourceAlpha += sourcePitch;
                    destinationAlpha += _pitch;
                }
                break;
            case 3:
                for(int y = top; y < bottom; ++y) {
                    auto *pixel = destinationAlpha;
                    for(int x = 0; x < left; ++x, pixel += 4) *pixel = 0;
                    for(int x = 0; x < copyWidth; ++x) {
                        pixel[x * 4] = (pixel[x * 4] * sourceAlpha[x * 4]) >> 8;
                    }
                    if(copyWidth > 0) pixel += copyWidth * 4;
                    for(int x = left + copyWidth; x < _width; ++x, pixel += 4) *pixel = 0;
                    sourceAlpha += sourcePitch;
                    destinationAlpha += _pitch;
                }
                break;
            default:
                TVPAddLog(TJS_W("layerExAlpha: noting type"));
                return;
        }
        for(int y = bottom; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) destinationAlpha[x * 4] = 0;
            destinationAlpha += _pitch;
        }
        redraw();
    }

    void ExMaskRect2(int dx, int dy, iTJSDispatch2 *sourceObject,
                 int sx, int sy, int width, int height, int type) {
        (void)sx;
        const int destinationWidth = _width, destinationHeight = _height;
        if(destinationWidth < dx || destinationHeight < dy) {
            if(destinationWidth <= dx)
                TVPAddLog(TJS_W("layerExAlpha: Src is position for OutArea:[Left]"));
            if(_height <= dy)
                TVPAddLog(TJS_W("layerExAlpha: Src is position for OutArea:[Top]"));
            return;
        }
        tTJSNI_Layer *source;
        if(TJS_FAILED(sourceObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&source))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int left = dx < 0 ? 0 : dx, top = dy < 0 ? 0 : dy;
        int copyWidth = width + (dx < 0 ? dx : 0);
        int copyHeight = height + (dy < 0 ? dy : 0);
        if(copyWidth + left > destinationWidth) copyWidth = destinationWidth - left;
        if(copyHeight + top > destinationHeight) copyHeight = destinationHeight - top;
        if(dy < 0) sy = -dy;
        const int sourcePitch = source->GetMainImagePixelBufferPitch();
        auto *sourceBuffer = static_cast<BufferT>(source->GetMainImagePixelBufferForWrite());
        auto *destinationAlpha = _buffer + _pitch * top + left * 4 + 3;
        auto *sourceAlpha = sourceBuffer + sourcePitch * sy + left * 4 + 3;
        const int bottom = top + copyHeight;
        switch(type) {
            case 0:
                for(int y = top; y < bottom; ++y) {
                    auto *pixel = destinationAlpha;
                    for(int x = 0; x < left; ++x, pixel += 4) *pixel = 0;
                    for(int x = 0; x < copyWidth; ++x) {
                        pixel[x * 4] = sourceAlpha[x * 4];
                    }
                    if(copyWidth > 0) pixel += copyWidth * 4;
                    for(int x = left + copyWidth; x < _width; ++x, pixel += 4) *pixel = 0;
                    sourceAlpha += sourcePitch;
                    destinationAlpha += _pitch;
                }
                break;
            case 1:
                for(int y = top; y < bottom; ++y) {
                    auto *pixel = destinationAlpha;
                    for(int x = 0; x < left; ++x, pixel += 4) *pixel = 0;
                    for(int x = 0; x < copyWidth; ++x) {
                        int alpha = pixel[x * 4] + sourceAlpha[x * 4];
                        pixel[x * 4] = alpha >= 255 ? 255 : alpha;
                    }
                    if(copyWidth > 0) pixel += copyWidth * 4;
                    for(int x = left + copyWidth; x < _width; ++x, pixel += 4) *pixel = 0;
                    sourceAlpha += sourcePitch;
                    destinationAlpha += _pitch;
                }
                break;
            case 2:
                for(int y = top; y < bottom; ++y) {
                    auto *pixel = destinationAlpha;
                    for(int x = 0; x < left; ++x, pixel += 4) *pixel = 0;
                    for(int x = 0; x < copyWidth; ++x) {
                        int alpha = pixel[x * 4] + sourceAlpha[x * 4] - 255;
                        pixel[x * 4] = alpha < 0 ? 0 : alpha;
                    }
                    if(copyWidth > 0) pixel += copyWidth * 4;
                    for(int x = left + copyWidth; x < _width; ++x, pixel += 4) *pixel = 0;
                    sourceAlpha += sourcePitch;
                    destinationAlpha += _pitch;
                }
                break;
            case 3:
                for(int y = top; y < bottom; ++y) {
                    auto *pixel = destinationAlpha;
                    for(int x = 0; x < left; ++x, pixel += 4) *pixel = 0;
                    for(int x = 0; x < copyWidth; ++x) {
                        pixel[x * 4] = (pixel[x * 4] * sourceAlpha[x * 4]) >> 8;
                    }
                    if(copyWidth > 0) pixel += copyWidth * 4;
                    for(int x = left + copyWidth; x < _width; ++x, pixel += 4) *pixel = 0;
                    sourceAlpha += sourcePitch;
                    destinationAlpha += _pitch;
                }
                break;
            default:
                TVPAddLog(TJS_W("layerExAlpha: noting type"));
                return;
        }
        redraw();
    }

    void LightForAlpha(iTJSDispatch2 *sourceObject) {
        tTJSNI_Layer *source;
        if(TJS_FAILED(sourceObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&source))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int sourcePitch = source->GetMainImagePixelBufferPitch();
        auto *input = static_cast<BufferT>(source->GetMainImagePixelBufferForWrite());
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) {
                auto *pixel = row + x * 4;
                pixel[3] = input[x * 4 + 1];
                pixel[0] = pixel[1] = 0;
                pixel[2] = 0;
            }
            input += sourcePitch;
            row += _pitch;
        }
        redraw();
    }

    void LuminanceForAlpha(iTJSDispatch2 *sourceObject) {
        tTJSNI_Layer *source;
        if(TJS_FAILED(sourceObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&source))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int sourcePitch = source->GetMainImagePixelBufferPitch();
        auto *input = static_cast<BufferT>(source->GetMainImagePixelBufferForWrite());
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) {
                auto *pixel = row + x * 4;
                pixel[3] = static_cast<int>((input[x * 4] * 0.29891f + input[x * 4 + 1] * 0.58661f) + input[x * 4 + 2] * 0.11448f);
                pixel[0] = pixel[1] = 0;
                pixel[2] = 0;
            }
            input += sourcePitch;
            row += _pitch;
        }
        redraw();
    }

    void LevelMaskRect(int center, int radius) {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        auto *row = _buffer;
        const int count = radius * 2 + 1;
        std::vector<int> levels;
        levels.resize(count);
        for(int i = 0; i < count; ++i) {
            if(i <= radius)
                levels[i] = static_cast<int>((255.0f / (radius + 1)) * (i + 1) + 0.5f);
            else if(i == radius + 1) levels[i] = 255;
            else levels[i] = static_cast<int>((255.0f / (radius + 1)) * (count - i) + 0.5f);
        }
        const int originalLow = center - radius;
        const int low = originalLow < 0 ? 0 : originalLow;
        const int high = center + radius >= 255 ? 255 : center + radius;
        for(int y = 0; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) {
                auto &alpha = row[x * 4 + 3];
                alpha = alpha >= low && alpha <= high ? levels[alpha - originalLow] : 0;
            }
            row += _pitch;
        }
        redraw();
    }

    void LevelMaskRect2(int center, int left, int right) {
        _buffer = static_cast<BufferT>(_this->GetMainImagePixelBufferForWrite());
        _pitch = _this->GetMainImagePixelBufferPitch();
        auto *row = _buffer;
        const int count = left + right + 1;
        std::vector<int> levels;
        levels.resize(count);
        for(int i = 0; i < count; ++i) {
            if(i < left)
                levels[i] = static_cast<int>((255.0f / (left + 1)) * (i + 1) + 0.5f);
            else if(i == left) levels[i] = 255;
            else levels[i] = static_cast<int>((255.0f / (right + 1)) * (count - i) + 0.5f);
            if(levels[i] >= 255) levels[i] = 255;
            else if(levels[i] <= 0) levels[i] = 0;
        }
        const int originalLow = center - left;
        const int low = originalLow < 0 ? 0 : originalLow;
        const int high = center + right >= 255 ? 255 : center + right;
        for(int y = 0; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) {
                auto &alpha = row[x * 4 + 3];
                alpha = alpha >= low && alpha <= high ? levels[alpha - originalLow] : 0;
            }
            row += _pitch;
        }
        redraw();
    }

    void MaskImageRideRect(iTJSDispatch2 *maskObject, int center, int left, int right,
                           int red, int green, int blue) {
        tTJSNI_Layer *mask;
        if(TJS_FAILED(maskObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&mask))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int maskPitch = mask->GetMainImagePixelBufferPitch();
        auto *maskBuffer = static_cast<BufferT>(mask->GetMainImagePixelBufferForWrite());
        auto *row = _buffer;
        const int count = left + right + 1;
        std::vector<float> coefficients;
        coefficients.resize(count);
        std::vector<int> reds;
        reds.resize(count);
        std::vector<int> greens;
        greens.resize(count);
        std::vector<int> blues;
        blues.resize(count);
        const double leftScale = 1.0 / static_cast<double>(left + 1);
        const double leftRed = static_cast<double>(red) / (left + 1);
        const double leftGreen = static_cast<float>(green) / (left + 1);
        const double leftBlue = static_cast<float>(blue) / (left + 1);
        const double rightScale = static_cast<float>(1.0 / static_cast<float>(right + 1));
        const double rightRed = static_cast<float>(red) / (right + 1);
        const double rightGreen = static_cast<float>(green) / (right + 1);
        const double rightBlue = static_cast<float>(blue) / (right + 1);
        for(int i = 0; i < count; ++i) {
            if(i < left) {
                coefficients[i] = leftScale * (left - i);
                reds[i] = static_cast<int>(leftRed * (i + 1) + 0.5);
                greens[i] = static_cast<int>(leftGreen * (i + 1) + 0.5);
                blues[i] = static_cast<int>(leftBlue * (i + 1) + 0.5);
            } else if(i == left) {
                coefficients[i] = 0.0f;
                reds[i] = red;
                greens[i] = green;
                blues[i] = blue;
            } else {
                coefficients[i] = rightScale * (i - left);
                reds[i] = static_cast<int>(rightRed * (count - i) + 0.5);
                greens[i] = static_cast<int>(rightGreen * (count - i) + 0.5);
                blues[i] = static_cast<int>(rightBlue * (count - i) + 0.5);
            }
            if(coefficients[i] >= 1.0f) coefficients[i] = 1.0f;
            if(reds[i] >= 255) reds[i] = 255;
            if(greens[i] >= 255) greens[i] = 255;
            if(blues[i] >= 255) blues[i] = 255;
            if(coefficients[i] <= 0.0f) coefficients[i] = 0.0f;
            if(reds[i] <= 0) reds[i] = 0;
            if(greens[i] <= 0) greens[i] = 0;
            if(blues[i] <= 0) blues[i] = 0;
        }
        const int originalLow = center - left;
        const int low = originalLow < 0 ? 0 : originalLow;
        const int high = center + right >= 255 ? 255 : center + right;
        for(int y = 0; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) {
                auto *pixel = row + x * 4;
                const int alpha = maskBuffer[x * 4 + 3];
                if(pixel[3] && alpha >= low && (alpha <= center || alpha < high)) {
                    const int index = alpha - originalLow;
                    pixel[0] = static_cast<int>(pixel[0] * coefficients[index] + 0.5f) + blues[index];
                    pixel[1] = static_cast<int>(pixel[1] * coefficients[index] + 0.5f) + greens[index];
                    pixel[2] = static_cast<int>(pixel[2] * coefficients[index] + 0.5f) + reds[index];
                }
            }
            row += _pitch;
            maskBuffer += maskPitch;
        }
        redraw();
    }

    void MaskImageRideRect2(iTJSDispatch2 *maskObject, int center, int radius,
                            int red, int green, int blue) {
        tTJSNI_Layer *mask;
        if(TJS_FAILED(maskObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&mask))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int maskPitch = mask->GetMainImagePixelBufferPitch();
        auto *maskBuffer = static_cast<BufferT>(mask->GetMainImagePixelBufferForWrite());
        auto *row = _buffer;
        const int count = radius + 1;
        std::vector<float> coefficients;
        coefficients.resize(count);
        std::vector<int> reds;
        reds.resize(count);
        std::vector<int> greens;
        greens.resize(count);
        std::vector<int> blues;
        blues.resize(count);
        const float scale = 1.0f / count;
        const float redScale = static_cast<float>(red) / count;
        const float greenScale = static_cast<float>(green) / count;
        const float blueScale = static_cast<float>(blue) / count;
        for(int i = 0; i < count; ++i) {
            if(i < radius) {
                coefficients[i] = scale * (radius - i);
                reds[i] = static_cast<int>(redScale * (i + 1) + 0.5f);
                greens[i] = static_cast<int>(greenScale * (i + 1) + 0.5f);
                blues[i] = static_cast<int>(blueScale * (i + 1) + 0.5f);
            } else if(i == radius) {
                coefficients[i] = 0.0f;
                reds[i] = red;
                greens[i] = green;
                blues[i] = blue;
            }
            if(coefficients[i] >= 1.0f) coefficients[i] = 1.0f;
            if(reds[i] >= 255) reds[i] = 255;
            if(greens[i] >= 255) greens[i] = 255;
            if(blues[i] >= 255) blues[i] = 255;
            if(coefficients[i] <= 0.0f) coefficients[i] = 0.0f;
            if(reds[i] <= 0) reds[i] = 0;
            if(greens[i] <= 0) greens[i] = 0;
            if(blues[i] <= 0) blues[i] = 0;
        }
        const int originalLow = center - radius;
        const int low = originalLow < 0 ? 0 : originalLow;
        for(int y = 0; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) {
                auto *pixel = row + x * 4;
                const int alpha = maskBuffer[x * 4 + 3];
                if(pixel[3] && alpha >= low) {
                    if(alpha <= center) {
                        const int index = alpha - originalLow;
                        pixel[0] = static_cast<int>(pixel[0] * coefficients[index] + 0.5f) + blues[index];
                        pixel[1] = static_cast<int>(pixel[1] * coefficients[index] + 0.5f) + greens[index];
                        pixel[2] = static_cast<int>(pixel[2] * coefficients[index] + 0.5f) + reds[index];
                    } else {
                        pixel[0] = blue;
                        pixel[1] = green;
                        pixel[2] = red;
                    }
                }
            }
            row += _pitch;
            maskBuffer += maskPitch;
        }
        redraw();
    }

    void MaskImageRideRect3(iTJSDispatch2 *maskObject, int center, int radius,
                            int red, int green, int blue) {
        tTJSNI_Layer *mask;
        if(TJS_FAILED(maskObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&mask))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int maskPitch = mask->GetMainImagePixelBufferPitch();
        auto *maskBuffer = static_cast<BufferT>(mask->GetMainImagePixelBufferForWrite());
        auto *row = _buffer;
        const int count = radius + 1;
        std::vector<float> coefficients;
        coefficients.resize(count);
        std::vector<int> reds;
        reds.resize(count);
        std::vector<int> greens;
        greens.resize(count);
        std::vector<int> blues;
        blues.resize(count);
        const float scale = 1.0f / count;
        const float redScale = static_cast<float>(red) / count;
        const float greenScale = static_cast<float>(green) / count;
        const float blueScale = static_cast<float>(blue) / count;
        for(int i = 0; i < count; ++i) {
            if(i == 0) {
                coefficients[i] = 0.0f;
                reds[i] = red;
                greens[i] = green;
                blues[i] = blue;
            } else if(i > 0) {
                coefficients[i] = scale * i;
                reds[i] = static_cast<int>(redScale * (count - i) + 0.5f);
                greens[i] = static_cast<int>(greenScale * (count - i) + 0.5f);
                blues[i] = static_cast<int>(blueScale * (count - i) + 0.5f);
            }
            if(coefficients[i] >= 1.0f) coefficients[i] = 1.0f;
            if(reds[i] >= 255) reds[i] = 255;
            if(greens[i] >= 255) greens[i] = 255;
            if(blues[i] >= 255) blues[i] = 255;
            if(coefficients[i] <= 0.0f) coefficients[i] = 0.0f;
            if(reds[i] <= 0) reds[i] = 0;
            if(greens[i] <= 0) greens[i] = 0;
            if(blues[i] <= 0) blues[i] = 0;
        }
        const int high = center + radius >= 255 ? 255 : center + radius;
        for(int y = 0; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) {
                auto *pixel = row + x * 4;
                const int alpha = maskBuffer[x * 4 + 3];
                if(pixel[3]) {
                    if(alpha < center) {
                        pixel[0] = blue;
                        pixel[1] = green;
                        pixel[2] = red;
                    } else if(alpha <= high) {
                        const int index = alpha - center;
                        pixel[0] = static_cast<int>(pixel[0] * coefficients[index] + 0.5f) + blues[index];
                        pixel[1] = static_cast<int>(pixel[1] * coefficients[index] + 0.5f) + greens[index];
                        pixel[2] = static_cast<int>(pixel[2] * coefficients[index] + 0.5f) + reds[index];
                    }
                }
            }
            row += _pitch;
            maskBuffer += maskPitch;
        }
        redraw();
    }

    void MaskImageRideRect4(iTJSDispatch2 *sourceObject, iTJSDispatch2 *maskObject,
                            int center, int radius, int red, int green, int blue) {
        tTJSNI_Layer *source;
        if(TJS_FAILED(sourceObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&source))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int sourcePitch = source->GetMainImagePixelBufferPitch();
        auto *sourceBuffer = static_cast<BufferT>(source->GetMainImagePixelBufferForWrite());
        const int sourceWidth = source->GetImageWidth();
        const int sourceHeight = source->GetImageHeight();
        tTJSNI_Layer *mask;
        if(TJS_FAILED(maskObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&mask))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int maskPitch = mask->GetMainImagePixelBufferPitch();
        auto *maskBuffer = static_cast<BufferT>(mask->GetMainImagePixelBufferForWrite());
        const int maskWidth = mask->GetImageWidth();
        const int maskHeight = mask->GetImageHeight();
        if(_width != sourceWidth || _width != maskWidth ||
           _height != sourceHeight || _height != maskHeight) {
            TVPAddLog(ttstr(TJS_W("SW:")) + ttstr(_width) + TJS_W(" SH:") + ttstr(_height));
            TVPAddLog(ttstr(TJS_W("DW:")) + ttstr(sourceWidth) + TJS_W(" DH:") + ttstr(sourceHeight));
            TVPAddLog(ttstr(TJS_W("AW:")) + ttstr(maskWidth) + TJS_W(" AH:") + ttstr(maskHeight));
            return;
        }
        auto *row = _buffer;
        const int count = radius * 2 - 1, midpoint = radius - 1;
        std::vector<float> coefficients;
        coefficients.resize(count);
        std::vector<int> reds;
        reds.resize(count);
        std::vector<int> greens;
        greens.resize(count);
        std::vector<int> blues;
        blues.resize(count);
        const float scale = 1.0f / radius;
        const float redScale = static_cast<float>(red) / radius;
        const float greenScale = static_cast<float>(green) / radius;
        const float blueScale = static_cast<float>(blue) / radius;
        for(int i = 0; i < count; ++i) {
            if(i < midpoint) {
                coefficients[i] = scale * (midpoint - i);
                reds[i] = static_cast<int>(redScale * (i + 1) + 0.5f);
                greens[i] = static_cast<int>(greenScale * (i + 1) + 0.5f);
                blues[i] = static_cast<int>(blueScale * (i + 1) + 0.5f);
            } else if(i == midpoint) {
                coefficients[i] = 0.0f;
                reds[i] = red;
                greens[i] = green;
                blues[i] = blue;
            } else {
                coefficients[i] = scale * (i - midpoint);
                reds[i] = static_cast<int>(redScale * (count - i) + 0.5f);
                greens[i] = static_cast<int>(greenScale * (count - i) + 0.5f);
                blues[i] = static_cast<int>(blueScale * (count - i) + 0.5f);
            }
            if(coefficients[i] >= 1.0f) coefficients[i] = 1.0f;
            if(reds[i] >= 255) reds[i] = 255;
            if(greens[i] >= 255) greens[i] = 255;
            if(blues[i] >= 255) blues[i] = 255;
            if(coefficients[i] <= 0.0f) coefficients[i] = 0.0f;
            if(reds[i] <= 0) reds[i] = 0;
            if(greens[i] <= 0) greens[i] = 0;
            if(blues[i] <= 0) blues[i] = 0;
        }
        int low = center - radius < 0 ? 0 : center - radius + 1;
        if(low >= 255) low = 255;
        int high = center + radius < 0 ? 0 : center + radius;
        if(high >= 255) high = 255;
        for(int y = 0; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) {
                auto *pixel = row + x * 4;
                auto *input = sourceBuffer + x * 4;
                if(pixel[3] || input[3]) {
                    const int alpha = maskBuffer[x * 4 + 3], index = alpha - low;
                    if(alpha < low) {
                        pixel[0] = input[0];
                        pixel[1] = input[1];
                        pixel[2] = input[2];
                    } else if(alpha < center) {
                        pixel[0] = static_cast<int>(static_cast<double>(input[0]) * coefficients[index] + 0.5) + blues[index];
                        pixel[1] = static_cast<int>(static_cast<double>(input[1]) * coefficients[index] + 0.5) + greens[index];
                        pixel[2] = static_cast<int>(static_cast<double>(input[2]) * coefficients[index] + 0.5) + reds[index];
                    } else if(alpha < high) {
                        pixel[0] = static_cast<int>(pixel[0] * coefficients[index] + 0.5f) + blues[index];
                        pixel[1] = static_cast<int>(pixel[1] * coefficients[index] + 0.5f) + greens[index];
                        pixel[2] = static_cast<int>(pixel[2] * coefficients[index] + 0.5f) + reds[index];
                    }
                }
            }
            row += _pitch;
            sourceBuffer += sourcePitch;
            maskBuffer += maskPitch;
        }
        redraw();
    }

    void MaskImageRideRect5(iTJSDispatch2 *sourceObject, iTJSDispatch2 *maskObject,
                            int center, int radius) {
        tTJSNI_Layer *source;
        if(TJS_FAILED(sourceObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&source))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int sourcePitch = source->GetMainImagePixelBufferPitch();
        auto *sourceBuffer = static_cast<BufferT>(source->GetMainImagePixelBufferForWrite());
        const int sourceWidth = source->GetImageWidth();
        const int sourceHeight = source->GetImageHeight();
        tTJSNI_Layer *mask;
        if(TJS_FAILED(maskObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&mask))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int maskPitch = mask->GetMainImagePixelBufferPitch();
        auto *maskBuffer = static_cast<BufferT>(mask->GetMainImagePixelBufferForWrite());
        const int maskWidth = mask->GetImageWidth();
        const int maskHeight = mask->GetImageHeight();
        if(_width != sourceWidth || _width != maskWidth ||
           _height != sourceHeight || _height != maskHeight) {
            TVPAddLog(ttstr(TJS_W("SW:")) + ttstr(_width) + TJS_W(" SH:") + ttstr(_height));
            TVPAddLog(ttstr(TJS_W("DW:")) + ttstr(sourceWidth) + TJS_W(" DH:") + ttstr(sourceHeight));
            TVPAddLog(ttstr(TJS_W("AW:")) + ttstr(maskWidth) + TJS_W(" AH:") + ttstr(maskHeight));
            return;
        }
        auto *row = _buffer;
        const int count = radius * 2 - 1, midpoint = radius - 1;
        std::vector<float> alphaWeights;
        alphaWeights.resize(count);
        std::vector<float> destinationWeights;
        destinationWeights.resize(count);
        std::vector<float> sourceWeights;
        sourceWeights.resize(count);
        const float scale = 1.0f / radius;
        for(int i = 0; i < count; ++i) {
            if(i < midpoint) {
                destinationWeights[i] = scale * (midpoint - i);
                sourceWeights[i] = 1.0f - destinationWeights[i];
                alphaWeights[i] = 1.0f;
            } else if(i == midpoint) {
                destinationWeights[i] = 0.0f;
                sourceWeights[i] = 1.0f;
                alphaWeights[i] = 1.0f;
            } else {
                destinationWeights[i] = 0.0f;
                sourceWeights[i] = 1.0f;
                alphaWeights[i] = scale * (count - i);
            }
            if(destinationWeights[i] >= 1.0f) destinationWeights[i] = 1.0f;
            if(sourceWeights[i] >= 1.0f) sourceWeights[i] = 1.0f;
            if(alphaWeights[i] >= 1.0f) alphaWeights[i] = 1.0f;
            if(destinationWeights[i] <= 0.0f) destinationWeights[i] = 0.0f;
            if(sourceWeights[i] <= 0.0f) sourceWeights[i] = 0.0f;
            if(alphaWeights[i] <= 0.0f) alphaWeights[i] = 0.0f;
        }
        int low = center - radius < 0 ? 0 : center - radius + 1;
        if(low >= 255) low = 255;
        int high = center + radius < 0 ? 0 : center + radius;
        if(high >= 255) high = 255;
        for(int y = 0; y < _height; ++y) {
            // The original uses its table count as the pixel extent here.
            for(int x = 0; x < count; ++x) {
                auto *pixel = row + x * 4;
                auto *input = sourceBuffer + x * 4;
                const int alpha = maskBuffer[x * 4 + 3];
                if((pixel[3] || input[3]) && alpha >= low) {
                    const int index = alpha - low;
                    if(alpha < center) {
                        pixel[0] = static_cast<int>(input[0] * sourceWeights[index] + 0.5f) +
                            static_cast<int>(pixel[0] * destinationWeights[index] + 0.5f);
                        pixel[1] = static_cast<int>(pixel[1] * destinationWeights[index] + 0.5f) +
                            static_cast<int>(input[1] * sourceWeights[index] + 0.5f);
                        pixel[2] = static_cast<int>(pixel[2] * destinationWeights[index] + 0.5f) +
                            static_cast<int>(input[2] * sourceWeights[index] + 0.5f);
                    } else if(alpha < high) {
                        pixel[0] = input[0];
                        pixel[1] = input[1];
                        pixel[2] = input[2];
                        pixel[3] = static_cast<int>(input[3] * alphaWeights[index] + 0.5f);
                    } else pixel[3] = 0;
                }
            }
            row += _pitch;
            sourceBuffer += sourcePitch;
            maskBuffer += maskPitch;
        }
        redraw();
    }

    void MaskImageRideRect7(iTJSDispatch2 *sourceObject, iTJSDispatch2 *maskObject,
                            int center, int radius) {
        tTJSNI_Layer *source;
        if(TJS_FAILED(sourceObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&source))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int sourcePitch = source->GetMainImagePixelBufferPitch();
        auto *sourceBuffer = static_cast<BufferT>(source->GetMainImagePixelBufferForWrite());
        const int sourceWidth = source->GetImageWidth();
        const int sourceHeight = source->GetImageHeight();
        tTJSNI_Layer *mask;
        if(TJS_FAILED(maskObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&mask))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int maskPitch = mask->GetMainImagePixelBufferPitch();
        auto *maskBuffer = static_cast<BufferT>(mask->GetMainImagePixelBufferForWrite());
        const int maskWidth = mask->GetImageWidth();
        const int maskHeight = mask->GetImageHeight();
        if(_width != sourceWidth || _width != maskWidth ||
           _height != sourceHeight || _height != maskHeight) {
            TVPAddLog(ttstr(TJS_W("SW:")) + ttstr(_width) + TJS_W(" SH:") + ttstr(_height));
            TVPAddLog(ttstr(TJS_W("DW:")) + ttstr(sourceWidth) + TJS_W(" DH:") + ttstr(sourceHeight));
            TVPAddLog(ttstr(TJS_W("AW:")) + ttstr(maskWidth) + TJS_W(" AH:") + ttstr(maskHeight));
            return;
        }
        auto *row = _buffer;
        const int count = radius * 2 - 1, midpoint = radius - 1;
        std::vector<float> coefficients;
        coefficients.resize(count);
        const float scale = 1.0f / radius;
        for(int i = 0; i < count; ++i) {
            if(i < midpoint) coefficients[i] = scale * (i + 1);
            else if(i == midpoint) coefficients[i] = 1.0f;
            else coefficients[i] = scale * (count - i);
            if(coefficients[i] >= 1.0f) coefficients[i] = 1.0f;
            if(coefficients[i] <= 0.0f) coefficients[i] = 0.0f;
        }
        int low = center - radius < 0 ? 0 : center - radius + 1;
        if(low >= 255) low = 255;
        int high = center + radius < 0 ? 0 : center + radius;
        if(high >= 255) high = 255;
        for(int y = 0; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) {
                auto *pixel = row + x * 4;
                auto *input = sourceBuffer + x * 4;
                const int alpha = maskBuffer[x * 4 + 3];
                if((pixel[3] || input[3]) && alpha >= low) {
                    if(alpha < high) {
                        pixel[0] = input[0];
                        pixel[1] = input[1];
                        pixel[2] = input[2];
                        pixel[3] = static_cast<int>(input[3] * coefficients[alpha - low] + 0.5f);
                    } else pixel[3] = 0;
                }
            }
            row += _pitch;
            sourceBuffer += sourcePitch;
            maskBuffer += maskPitch;
        }
        redraw();
    }

    void MaskImageRideRect8(iTJSDispatch2 *sourceObject, iTJSDispatch2 *maskObject, int level) {
        tTJSNI_Layer *source;
        if(TJS_FAILED(sourceObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&source))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int sourcePitch = source->GetMainImagePixelBufferPitch();
        auto *sourceBuffer = static_cast<BufferT>(source->GetMainImagePixelBufferForWrite());
        const int sourceWidth = source->GetImageWidth();
        const int sourceHeight = source->GetImageHeight();
        tTJSNI_Layer *mask;
        if(TJS_FAILED(maskObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&mask))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int maskPitch = mask->GetMainImagePixelBufferPitch();
        auto *maskBuffer = static_cast<BufferT>(mask->GetMainImagePixelBufferForWrite());
        const int maskWidth = mask->GetImageWidth();
        const int maskHeight = mask->GetImageHeight();
        if(_width != sourceWidth || _width != maskWidth ||
           _height != sourceHeight || _height != maskHeight) {
            TVPAddLog(ttstr(TJS_W("SW:")) + ttstr(_width) + TJS_W(" SH:") + ttstr(_height));
            TVPAddLog(ttstr(TJS_W("DW:")) + ttstr(sourceWidth) + TJS_W(" DH:") + ttstr(sourceHeight));
            TVPAddLog(ttstr(TJS_W("AW:")) + ttstr(maskWidth) + TJS_W(" AH:") + ttstr(maskHeight));
            return;
        }
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) {
                auto *pixel = row + x * 4;
                auto *input = sourceBuffer + x * 4;
                if((pixel[3] || input[3]) && maskBuffer[x * 4 + 3] > level) {
                    pixel[0] = input[0];
                    pixel[1] = input[1];
                    pixel[2] = input[2];
                    pixel[3] = input[3];
                }
            }
            row += _pitch;
            sourceBuffer += sourcePitch;
            maskBuffer += maskPitch;
        }
        redraw();
    }

    void AlphaBorderInnerRect(iTJSDispatch2 *maskObject, int level) {
        tTJSNI_Layer *mask;
        if(TJS_FAILED(maskObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&mask))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int maskPitch = mask->GetMainImagePixelBufferPitch();
        auto *maskBuffer = static_cast<BufferT>(mask->GetMainImagePixelBufferForWrite());
        const int maskWidth = mask->GetImageWidth();
        const int maskHeight = mask->GetImageHeight();
        if(_width != maskWidth) {
            TVPAddLog(ttstr(TJS_W("SW:")) + ttstr(_width) + TJS_W(" SH:") + ttstr(_height));
            TVPAddLog(ttstr(TJS_W("AW:")) + ttstr(maskWidth) + TJS_W(" AH:") + ttstr(maskHeight));
            return;
        }
        auto *row = _buffer;
        for(int y = 0; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) {
                if(maskBuffer[x * 4 + 3] >= level) row[x * 4 + 3] = 0;
            }
            row += _pitch;
            maskBuffer += maskPitch;
        }
        redraw();
    }

    void AlphaBorderImageCoverRect(iTJSDispatch2 *sourceObject,
                                   iTJSDispatch2 *alphaObject,
                                   iTJSDispatch2 *maskObject, int center, int radius) {
        tTJSNI_Layer *source;
        if(TJS_FAILED(sourceObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&source))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int sourcePitch = source->GetMainImagePixelBufferPitch();
        auto *sourceBuffer = static_cast<BufferT>(source->GetMainImagePixelBufferForWrite());
        const int sourceWidth = source->GetImageWidth();
        const int sourceHeight = source->GetImageHeight();
        tTJSNI_Layer *alpha;
        if(TJS_FAILED(alphaObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&alpha))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int alphaPitch = alpha->GetMainImagePixelBufferPitch();
        auto *alphaBuffer = static_cast<BufferT>(alpha->GetMainImagePixelBufferForWrite());
        const int alphaWidth = alpha->GetImageWidth();
        const int alphaHeight = alpha->GetImageHeight();
        tTJSNI_Layer *mask;
        if(TJS_FAILED(maskObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&mask))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int maskPitch = mask->GetMainImagePixelBufferPitch();
        auto *maskBuffer = static_cast<BufferT>(mask->GetMainImagePixelBufferForWrite());
        const int maskWidth = mask->GetImageWidth();
        const int maskHeight = mask->GetImageHeight();
        (void)alphaWidth;
        (void)alphaHeight;
        if(_width != sourceWidth || _width != maskWidth ||
           _height != sourceHeight || _height != maskHeight) {
            TVPAddLog(ttstr(TJS_W("SW:")) + ttstr(_width) + TJS_W(" SH:") + ttstr(_height));
            TVPAddLog(ttstr(TJS_W("DW:")) + ttstr(sourceWidth) + TJS_W(" DH:") + ttstr(sourceHeight));
            TVPAddLog(ttstr(TJS_W("AW:")) + ttstr(maskWidth) + TJS_W(" AH:") + ttstr(maskHeight));
            return;
        }
        if(radius < 1 || center + radius < 1 || center - radius > 254) {
            TVPAddLog(TJS_W("Out of range"));
            return;
        }
        auto *row = _buffer;
        int left, right;
        if(center < 0) {
            left = 0;
            right = center + radius < 256 ? radius : 255 - center + radius;
        } else if(center >= radius) {
            left = radius;
            if(center >= 256) {
                left = center + radius - 255;
                right = 0;
            } else right = center + radius < 256 ? radius : 255 - center + radius;
        } else {
            left = center;
            right = center <= 255 ?
                (center + radius < 256 ? radius : 255 - center + radius) : 0;
        }
        if(left < 0) left = 0;
        if(right < 0) right = 0;
        const int count = left + right + 1;
        std::vector<float> coefficients;
        coefficients.resize(count);
        const float scale = 1.0f / radius;
        for(int i = 0; i < count; ++i) {
            if(i < left) coefficients[i] = scale * (radius - left + i);
            else if(i == left) coefficients[i] = 1.0f;
            else coefficients[i] = scale * (radius + left + 1 - i);
            if(coefficients[i] >= 1.0f) coefficients[i] = 1.0f;
            if(coefficients[i] <= 0.0f) coefficients[i] = 0.0f;
        }
        const int low = center - left;
        for(int y = 0; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) {
                auto *pixel = row + x * 4;
                auto *input = sourceBuffer + x * 4;
                const int alpha = maskBuffer[x * 4 + 3];
                if((pixel[3] || input[3]) && alpha >= low && alpha < center + right) {
                    pixel[0] = input[0];
                    pixel[1] = input[1];
                    pixel[2] = input[2];
                    pixel[3] = static_cast<int>(alphaBuffer[x * 4 + 3] * coefficients[alpha - low] + 0.5f);
                }
            }
            row += _pitch;
            alphaBuffer += alphaPitch;
            sourceBuffer += sourcePitch;
            maskBuffer += maskPitch;
        }
        redraw();
    }

    void MaskImageEffectRect(iTJSDispatch2 *maskObject, int center, int left, int right,
                             int red, int green, int blue) {
        tTJSNI_Layer *mask;
        if(TJS_FAILED(maskObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&mask))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));
        const int maskPitch = mask->GetMainImagePixelBufferPitch();
        auto *maskBuffer = static_cast<BufferT>(mask->GetMainImagePixelBufferForWrite());
        const int maskWidth = mask->GetImageWidth();
        const int maskHeight = mask->GetImageHeight();
        (void)maskWidth;
        (void)maskHeight;
        auto *row = _buffer;
        const int count = left + right + 1;
        std::vector<float> coefficients;
        coefficients.resize(count);
        std::vector<unsigned int> alphas;
        alphas.resize(count);
        std::vector<unsigned int> reds;
        reds.resize(count);
        std::vector<unsigned int> greens;
        greens.resize(count);
        std::vector<unsigned int> blues;
        blues.resize(count);
        const float leftScale = 1.0f / (left + 1);
        const float leftAlpha = 255.0f / (left + 1);
        const float leftGreen = static_cast<float>(green) / (left + 1);
        const float leftBlue = static_cast<float>(blue) / (left + 1);
        const float rightScale = 1.0f / (right + 1);
        const float rightRed = static_cast<float>(red) / (right + 1);
        const float rightGreen = static_cast<float>(green) / (right + 1);
        const float rightBlue = static_cast<float>(blue) / (right + 1);
        for(int i = 0; i < count; ++i) {
            if(i < left) {
                coefficients[i] = leftScale * (left - i);
                alphas[i] = static_cast<unsigned int>(leftAlpha * (i + 1) + 0.5f);
                reds[i] = static_cast<unsigned int>(static_cast<float>((red / (left + 1)) * (i + 1)) + 0.5f);
                greens[i] = static_cast<unsigned int>(leftGreen * (i + 1) + 0.5f);
                blues[i] = static_cast<unsigned int>(leftBlue * (i + 1) + 0.5f);
            } else if(i == left) {
                coefficients[i] = 0.0f;
                alphas[i] = 255;
                reds[i] = red;
                greens[i] = green;
                blues[i] = blue;
            } else {
                coefficients[i] = rightScale * (i - left);
                alphas[i] = 255;
                reds[i] = static_cast<unsigned int>(rightRed * (count - i) + 0.5f);
                greens[i] = static_cast<unsigned int>(rightGreen * (count - i) + 0.5f);
                blues[i] = static_cast<unsigned int>(rightBlue * (count - i) + 0.5f);
            }
            if(coefficients[i] >= 1.0f) coefficients[i] = 1.0f;
            if(alphas[i] >= 255) alphas[i] = 255;
            if(reds[i] >= 255) reds[i] = 255;
            if(greens[i] >= 255) greens[i] = 255;
            if(blues[i] >= 255) blues[i] = 255;
            if(coefficients[i] <= 0.0f) coefficients[i] = 0.0f;
            if(!alphas[i]) alphas[i] = 0;
            if(!reds[i]) reds[i] = 0;
            if(!greens[i]) greens[i] = 0;
            if(!blues[i]) blues[i] = 0;
        }
        const int originalLow = center - left;
        const int low = originalLow < 0 ? 0 : originalLow;
        const int high = center + right >= 255 ? 255 : center + right;
        for(int y = 0; y < _height; ++y) {
            for(int x = 0; x < _width; ++x) {
                auto *pixel = row + x * 4;
                if(pixel[3]) {
                    const int alpha = maskBuffer[x * 4 + 3];
                    if(alpha < low) pixel[3] = 0;
                    else if(alpha <= center) {
                        const int index = alpha - originalLow;
                        pixel[0] = static_cast<int>(pixel[0] * coefficients[index] + 0.5f) + blues[index];
                        pixel[1] = static_cast<int>(pixel[1] * coefficients[index] + 0.5f) + greens[index];
                        pixel[2] = static_cast<int>(pixel[2] * coefficients[index] + 0.5f) + reds[index];
                        pixel[3] = alphas[index];
                    } else if(alpha < high) {
                        const int index = alpha - originalLow;
                        pixel[0] = static_cast<int>(pixel[0] * coefficients[index] + 0.5f) + blues[index];
                        pixel[1] = static_cast<int>(pixel[1] * coefficients[index] + 0.5f) + greens[index];
                        pixel[2] = static_cast<int>(pixel[2] * coefficients[index] + 0.5f) + reds[index];
                    }
                }
            }
            row += _pitch;
            maskBuffer += maskPitch;
        }
        redraw();
    }

    void MaskRect(int dx, int dy, iTJSDispatch2 *sourceObject,
                  int sx, int sy, int width, int height, int type) {
        tTJSNI_Layer *source;
        if(TJS_FAILED(sourceObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&source))))
            TVPThrowExceptionMessage(TJS_W("Not Layer"));

        if(type == 0) {
            _this->GetMainImage()->CopyRect(
                dx, dy, source->GetMainImage(),
                tTVPRect(sx, sy, sx + width, sy + height), 2);
        } else if(TVPIsSoftwareRenderManager()) {
            const int sourcePitch = source->GetMainImagePixelBufferPitch();
            // The original accessor marks the source writable even though
            // this operation consumes only its alpha bytes.
            const auto *sourceAlpha =
                static_cast<const unsigned char *>(
                    source->GetMainImagePixelBufferForWrite()) +
                sourcePitch * sy + sx * 4 + 3;
            auto *destinationAlpha = _buffer + _pitch * dy + dx * 4 + 3;
            switch(type) {
                case 1:
                    for(int y = 0; y < height; ++y) {
                        for(int x = 0; x < width; ++x) {
                            int alpha = destinationAlpha[x * 4] + sourceAlpha[x * 4];
                            destinationAlpha[x * 4] = alpha >= 255 ? 255 : alpha;
                        }
                        sourceAlpha += sourcePitch;
                        destinationAlpha += _pitch;
                    }
                    break;
                case 2:
                    for(int y = 0; y < height; ++y) {
                        for(int x = 0; x < width; ++x) {
                            int alpha = destinationAlpha[x * 4] + sourceAlpha[x * 4] - 255;
                            destinationAlpha[x * 4] = alpha < 0 ? 0 : alpha;
                        }
                        sourceAlpha += sourcePitch;
                        destinationAlpha += _pitch;
                    }
                    break;
                case 3:
                    for(int y = 0; y < height; ++y) {
                        for(int x = 0; x < width; ++x)
                            destinationAlpha[x * 4] =
                                (destinationAlpha[x * 4] * sourceAlpha[x * 4]) >> 8;
                        sourceAlpha += sourcePitch;
                        destinationAlpha += _pitch;
                    }
                    break;
                default:
                    TVPAddLog(TJS_W("layerExAlpha: noting type"));
                    break;
            }
        } else {
            iTVPRenderMethod *method;
            switch(type) {
                case 1: {
                    static iTVPRenderMethod *cached = TVPGetRenderManager()
                        ->CompileRenderMethod(
                            "ExAlphaAdd",
                            "void main() { gl_FragColor = texture2D(tex0, v_texCoord0); }\n",
                            1, 0)
                        ->SetBlendFuncSeparate(GL_FUNC_ADD, GL_ZERO, GL_ONE,
                                               GL_ONE, GL_ONE);
                    method = cached;
                    break;
                }
                case 2: {
                    static iTVPRenderMethod *cached = TVPGetRenderManager()
                        ->CompileRenderMethod(
                            "ExAlphaSub",
                            "void main() { gl_FragColor = vec4(0,0,0,1-texture2D(tex0, v_texCoord0).a); }\n",
                            1, 0)
                        ->SetBlendFuncSeparate(GL_FUNC_REVERSE_SUBTRACT,
                                               GL_ZERO, GL_ONE, GL_ONE, GL_ONE);
                    method = cached;
                    break;
                }
                case 3: {
                    static tjs_uint32 methodHint = 0;
                    static iTVPRenderMethod *cached = TVPGetRenderManager()
                        ->GetOrCompileRenderMethod(
                            "AlphaMask", &methodHint,
                            "void main() { gl_FragColor = texture2D(tex0, v_texCoord0); }\n",
                            1, 0)
                        ->SetBlendFuncSeparate(GL_FUNC_ADD, GL_ZERO, GL_ONE,
                                               GL_ZERO, GL_SRC_ALPHA);
                    method = cached;
                    break;
                }
            }
            const tRenderTexRectArray::Element sources[] = {
                {source->GetMainImage()->GetTexture(),
                 tTVPRect(sx, sy, sx + width, sy + height)}
            };
            TVPGetRenderManager()->OperateRect(
                method, _this->GetMainImage()->GetTextureForRender(true, nullptr),
                nullptr, tTVPRect(dx, dy, dx + width, dy + height),
                tRenderTexRectArray(sources));
        }
        redraw();
    }
};

NCB_GET_INSTANCE_HOOK(layerExAlpha) {
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj) {
            obj = new ClassT(objthis);
            SetNativeInstance(objthis, obj);
        }
        obj->reset();
        return obj;
    }
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
};

NCB_ATTACH_CLASS_WITH_HOOK(layerExAlpha, Layer) {
    NCB_METHOD(turnAlpha);
    NCB_METHOD(turnAlpha2);
    NCB_METHOD(turnAlpha3);
    NCB_METHOD(turnOutline);
    NCB_METHOD(AlphaColorRect);
    NCB_METHOD(AlphaDivide);
    NCB_METHOD(AlphaAdd);
    NCB_METHOD(AlphaAdd2);
    NCB_METHOD(BlackForAlpha);
    NCB_METHOD(AlphaBlend);
    NCB_METHOD(ColorAlphaAdd);
    NCB_METHOD(ColorVectorEffectAdd);
    NCB_METHOD(ExtraColorVectorEffectAdd);
    NCB_METHOD(WhiteToBlueBlurAdd);
    NCB_METHOD(MaskRect);
    NCB_METHOD(ExMaskRect);
    NCB_METHOD(ExMaskRect2);
    NCB_METHOD(LightForAlpha);
    NCB_METHOD(LuminanceForAlpha);
    NCB_METHOD(LevelMaskRect);
    NCB_METHOD(LevelMaskRect2);
    NCB_METHOD(MaskImageRideRect);
    NCB_METHOD(MaskImageRideRect2);
    NCB_METHOD(MaskImageRideRect3);
    NCB_METHOD(MaskImageRideRect4);
    NCB_METHOD(MaskImageRideRect5);
    NCB_METHOD(MaskImageRideRect7);
    NCB_METHOD(MaskImageRideRect8);
    NCB_METHOD(AlphaBorderInnerRect);
    NCB_METHOD(AlphaBorderImageCoverRect);
    NCB_METHOD(MaskImageEffectRect);
}
