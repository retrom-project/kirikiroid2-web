#include "tjsCommHead.h"
#include "tjsArray.h"
#include "tjsNative.h"
#include "FontImpl.h"
#include "StorageIntf.h"

namespace {

tjs_error AddFont(tTJSVariant *result, tjs_int numparams,
                  tTJSVariant **param, iTJSDispatch2 *) {
    if(numparams < 1) return TJS_E_BADPARAMCOUNT;

    // KiriKiri Z returns family names here; addFont.dll's System method
    // returns a count. The browser uses its VFS/FreeType font backend rather
    // than the Windows rasterizer or a native DLL.
    const ttstr path = TVPGetPlacedPath(*param[0]);
    const bool loaded = !path.IsEmpty() && TVPEnumFontsProc(path) > 0;
    if(!result) return TJS_S_OK;

    iTJSDispatch2 *array = TJSCreateArrayObject();
    tTJSVariant value(array, array);
    array->Release();
    tjs_int index = 0;
    if(loaded) {
        for(auto it = TVPFontNames.GetFirst(); !it.IsNull(); ++it) {
            if(it.GetValue().Path != path) continue;
            tTJSVariant family(it.GetKey());
            const tjs_error error = array->PropSetByNum(
                TJS_MEMBERENSURE, index++, &family, array);
            if(TJS_FAILED(error)) return error;
        }
    }
    *result = value;
    return TJS_S_OK;
}

} // namespace

void TVPRegisterWebFontMethods(tTJSNativeClass *font) {
    TJSNativeClassRegisterNCM(font, TJS_W("addFont"),
                             TJSCreateNativeClassMethod(AddFont),
                             font->GetClassName().c_str(), nitMethod,
                             TJS_STATICMEMBER);
}
