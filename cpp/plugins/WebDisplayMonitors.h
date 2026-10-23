#pragma once

#include <algorithm>
#include <limits>

#include "ncbind.hpp"

extern bool TVPGetScreenSize(tjs_int index, tjs_int &width, tjs_int &height);

namespace WindowExWeb {

// Browser boundary: an embedded canvas cannot enumerate/move native desktop
// windows. Expose one virtual display in the core's existing logical screen
// coordinates, not the canvas backing pixels (which include devicePixelRatio).
// The virtual display has no OS taskbar, so its work area is its full extent.
// Contract: https://github.com/wamsoft/windowEx/blob/518a5897a1803507de8a4cb92dc08f4f42561180/manual.tjs#L554
// This Web adaptation is not a reconstruction of the four native references.
inline tjs_error getDisplayMonitors(tTJSVariant *result, tjs_int numparams,
                                   tTJSVariant **param) {
    if(numparams != 0 && numparams != 4)
        return TJS_E_BADPARAMCOUNT;
    if(!result)
        return TJS_S_OK;

    tjs_int width, height;
    if(!TVPGetScreenSize(0, width, height) || width <= 0 || height <= 0)
        return TJS_E_FAIL;

    ncbArrayAccessor monitors;
    if(!monitors.IsValid())
        return TJS_E_FAIL;

    tjs_int64 left = 0, top = 0, right = width, bottom = height;
    if(numparams == 4) {
        tjs_int64 rect[4];
        for(tjs_int i = 0; i < 4; ++i) {
            rect[i] = static_cast<tjs_int64>(*param[i]);
            if(rect[i] < std::numeric_limits<tjs_int>::min() ||
               rect[i] > std::numeric_limits<tjs_int>::max())
                return TJS_E_INVALIDPARAM;
        }
        left = std::max<tjs_int64>(0, rect[0]);
        top = std::max<tjs_int64>(0, rect[1]);
        right = std::min<tjs_int64>(width, rect[0] + rect[2]);
        bottom = std::min<tjs_int64>(height, rect[1] + rect[3]);
        if(rect[2] <= 0 || rect[3] <= 0 || left >= right || top >= bottom) {
            result->SetObject(monitors.GetDispatch(), monitors.GetDispatch());
            return TJS_S_OK;
        }
    }

    const auto setRect = [](ncbDictionaryAccessor &rect, tjs_int x, tjs_int y,
                            tjs_int w, tjs_int h) {
        return rect.IsValid() && rect.SetValue(TJS_W("x"), x) &&
            rect.SetValue(TJS_W("y"), y) && rect.SetValue(TJS_W("w"), w) &&
            rect.SetValue(TJS_W("h"), h);
    };
    ncbDictionaryAccessor info, monitor, work, intersect;
    if(!info.IsValid() || !setRect(monitor, 0, 0, width, height) ||
       !setRect(work, 0, 0, width, height) ||
       !setRect(intersect, static_cast<tjs_int>(left), static_cast<tjs_int>(top),
                static_cast<tjs_int>(right - left),
                static_cast<tjs_int>(bottom - top)) ||
       !info.SetValue(TJS_W("name"), tTJSVariant(TJS_W("WebViewport"))) ||
       !info.SetValue(TJS_W("primary"), true) ||
       !info.SetValue(TJS_W("monitor"), monitor.GetDispatch()) ||
       !info.SetValue(TJS_W("work"), work.GetDispatch()) ||
       !info.SetValue(TJS_W("intersect"), intersect.GetDispatch()) ||
       !monitors.SetValue(tjs_int32(0), info.GetDispatch()))
        return TJS_E_FAIL;

    result->SetObject(monitors.GetDispatch(), monitors.GetDispatch());
    return TJS_S_OK;
}

} // namespace WindowExWeb
