#ifndef TVP_HOST_RESOURCE_OVERLAY_H
#define TVP_HOST_RESOURCE_OVERLAY_H
#ifdef __EMSCRIPTEN__
#include "tjsString.h"
TJS::ttstr TVPWebFindResourceOverlay(const TJS::ttstr &name);
#endif
#endif
