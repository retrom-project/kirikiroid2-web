#ifndef TVP_WEB_POINTER_EVENTS_H
#define TVP_WEB_POINTER_EVENTS_H

#include "tjsNative.h"

class tTJSNI_BaseWindow;

// The Web Z boundary adds Pointer callbacks after the existing Mouse/Touch
// delivery. The dispatch reference keeps its native instance alive if an old
// callback releases the last script reference.
class tTVPWebPointerEvent {
    tTJSNI_BaseWindow *window;
    iTJSDispatch2 *owner = nullptr;

public:
    enum class Phase { Down, Move, Up };
    explicit tTVPWebPointerEvent(tTJSNI_BaseWindow *window);
    ~tTVPWebPointerEvent();
    tTVPWebPointerEvent(const tTVPWebPointerEvent &) = delete;
    tTVPWebPointerEvent &operator=(const tTVPWebPointerEvent &) = delete;

    void Deliver(Phase phase, tjs_int kind, tjs_real x, tjs_real y,
                 tjs_real cx, tjs_real cy, tjs_uint32 shift,
                 tjs_uint32 id) const;
};

#endif
