#define NCB_MODULE_NAME TJS_W("LayerExSave.dll")
#include "ncbind.hpp"

#include "LayerImpl.h"
#include "MsgIntf.h"
#include "RenderManager.h"

static tjs_uint8 **getLayerRows(iTJSDispatch2 *object, tjs_int &width,
                                tjs_int &height, bool writable) {
    tTJSNI_Layer *layer = nullptr;
    if(object && TJS_FAILED(object->NativeInstanceSupport(
                     TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                     (iTJSNativeInstance **)&layer)))
        TVPThrowExceptionMessage(TVPSpecifyLayer);

    tTVPBaseTexture *image = layer->GetMainImage();
    iTVPTexture2D *texture = writable
        ? image->GetTextureForRender(false, nullptr) : image->GetTexture();
    width = texture->GetWidth();
    height = texture->GetHeight();
    tjs_uint8 **rows = new tjs_uint8 *[height];

    if(writable) {
        rows[0] = static_cast<tjs_uint8 *>(texture->GetScanLineForWrite(0));
        const tjs_int pitch = texture->GetPitch();
        for(tjs_int y = 1; y < height; y++)
            rows[y] = rows[y - 1] + pitch;
    } else {
        for(tjs_int y = 0; y < height; y++)
            rows[y] = const_cast<tjs_uint8 *>(
                static_cast<const tjs_uint8 *>(texture->GetScanLineForRead(y)));
    }
    return rows;
}

static tjs_error copyBlueToAlpha(tTJSVariant *, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *layer) {
    if(numparams < 0)
        return TJS_E_BADPARAMCOUNT;

    tjs_int sourceWidth, sourceHeight, destWidth, destHeight;
    tjs_uint8 **source = getLayerRows(param[0]->AsObjectNoAddRef(), sourceWidth,
                                      sourceHeight, false);
    tjs_uint8 **dest = getLayerRows(layer, destWidth, destHeight, true);
    const tjs_int width = sourceWidth < destWidth ? sourceWidth : destWidth;
    const tjs_int height = sourceHeight < destHeight ? sourceHeight : destHeight;
    for(tjs_int y = 0; y < height; y++) {
        for(tjs_int x = 0; x < width; x++)
            dest[y][4 * x + 3] = source[y][4 * x];
    }
    delete[] source;
    delete[] dest;
    return TJS_S_OK;
}

NCB_ATTACH_FUNCTION(copyBlueToAlpha, Layer, copyBlueToAlpha);
