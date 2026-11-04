// Web platform boundary for the supplied Win32 layeredwindow.dll contract.
// The four mobile references do not contain this module; see analysis.
#define NCB_MODULE_NAME TJS_W("layeredwindow.dll")
#include "ncbind.hpp"
#include "WindowImpl.h"
#include "TVPWindow.h"
#include "cocos2d.h"
#include <emscripten/heap.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

namespace {
constexpr const char *PresenterName = "krkr2.layeredwindow";

tjs_error LayeredWindow(tTJSVariant *, tjs_int count, tTJSVariant **params,
                        iTJSDispatch2 *) {
    // The Windows DLL reads five arguments even though it only checks for one.
    // A browser boundary must validate the complete linear-memory read.
    if(count < 5)
        return TJS_E_BADPARAMCOUNT;
    const tjs_int64 handle = *params[0];
    const tjs_int64 address = *params[1];
    const tjs_int pitch = *params[2];
    const tjs_int width = *params[3];
    const tjs_int height = *params[4];
    const auto invalid = [&](const char *reason) {
        std::fprintf(stderr,
            "[layeredwindow] rejected %s handle=%lld address=%lld pitch=%d size=%dx%d windows=%d\n",
            reason, static_cast<long long>(handle), static_cast<long long>(address),
            pitch, width, height, TVPGetWindowCount());
        return TJS_E_INVALIDPARAM;
    };
    tTJSNI_Window *window = nullptr;
    for(tjs_int i = 0; i < TVPGetWindowCount(); ++i) {
        auto *candidate = TVPGetWindowListAt(i);
        if(static_cast<tjs_int64>(reinterpret_cast<std::uintptr_t>(candidate)) == handle) {
            window = candidate;
            break;
        }
    }
    // A closing window can repaint from an immediate mouse-leave event. The
    // Windows plugin ignores failed HWND updates and still returns success.
    if(!window || !window->GetForm())
        return TJS_S_OK;
    auto *area = window->GetForm()->GetPrimaryArea();
    if(!area || address <= 0 || width <= 0 || height <= 0)
        return invalid("area-or-source");

    const std::int64_t rowBytes = static_cast<std::int64_t>(width) * 4;
    const std::int64_t lastRow = static_cast<std::int64_t>(height - 1) * pitch;
    const std::int64_t heapSize = emscripten_get_heap_size();
    if(address > heapSize || lastRow < -address ||
       lastRow > heapSize - address ||
       rowBytes > heapSize - address - std::max<std::int64_t>(0, lastRow))
        return invalid("heap-range");
    if(height > std::numeric_limits<ssize_t>::max() / rowBytes)
        return invalid("output-size");
    const std::int64_t outputBytes = rowBytes * height;

    // Positive-height DIB rows are bottom-up. Texture2D/Sprite expects top-down
    // RGBA; snapshot the borrowed BGRA buffer and preserve integer /255 alpha.
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(outputBytes));
    for(tjs_int y = 0; y < height; ++y) {
        const auto *src = reinterpret_cast<const std::uint8_t *>(
            static_cast<std::uintptr_t>(address +
                static_cast<std::int64_t>(height - 1 - y) * pitch));
        auto *dst = pixels.data() + static_cast<std::size_t>(y) * rowBytes;
        for(tjs_int x = 0; x < width; ++x, src += 4, dst += 4) {
            const unsigned alpha = src[3];
            if(alpha == 0) {
                dst[0] = dst[1] = dst[2] = dst[3] = 0;
            } else if(alpha == 255) {
                dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3];
            } else {
                dst[0] = src[2] * alpha / 255;
                dst[1] = src[1] * alpha / 255;
                dst[2] = src[0] * alpha / 255;
                dst[3] = alpha;
            }
        }
    }

    auto release = [](cocos2d::Texture2D *texture) { texture->release(); };
    std::unique_ptr<cocos2d::Texture2D, decltype(release)> texture(
        new cocos2d::Texture2D, release);
    if(!texture->initWithData(pixels.data(), static_cast<ssize_t>(outputBytes),
            cocos2d::Texture2D::PixelFormat::RGBA8888, width, height,
            cocos2d::Size(width, height)))
        return TJS_E_FAIL;

    auto *presenter = static_cast<cocos2d::Sprite *>(area->getChildByName(PresenterName));
    if(!presenter) {
        presenter = cocos2d::Sprite::createWithTexture(texture.get());
        if(!presenter)
            return TJS_E_FAIL;
        // UpdateLayeredWindow replaces the window's ordinary composition.
        // Hide only its Cocos output: the original TJS Layers still receive
        // modal-window button input and supply future layered bitmap updates.
        for(auto *child : area->getChildren())
            child->setVisible(false);
        presenter->setName(PresenterName);
        area->addChild(presenter, std::numeric_limits<int>::max());
    } else {
        presenter->setTexture(texture.get());
    }
    presenter->setTextureRect(cocos2d::Rect(0, 0, width, height));
    presenter->setAnchorPoint(cocos2d::Vec2(0, 1));
    presenter->setNormalizedPosition(cocos2d::Vec2(0, 1));
    presenter->setBlendFunc(cocos2d::BlendFunc::ALPHA_PREMULTIPLIED);
    presenter->setOpacityModifyRGB(true);
    // The existing Web window manager fits the active window to the canvas.
    // Reuse its primary-area transform for pixels and input; this does not
    // create desktop windows or change Window.showModal/confirmation behavior.
    return TJS_S_OK;
}

tjs_error Is2kOver(tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    // The supplied DLL returns success without writing its result variant.
    return TJS_S_OK;
}
} // namespace

NCB_REGISTER_FUNCTION(layeredwindow, LayeredWindow);
NCB_REGISTER_FUNCTION(is2kover, Is2kOver);
