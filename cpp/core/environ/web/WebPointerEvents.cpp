#include "WebPointerEvents.h"
#include "ScriptMgnIntf.h"
#include "WindowIntf.h"
#include "EventIntf.h"

tTVPWebPointerEvent::tTVPWebPointerEvent(tTJSNI_BaseWindow *window) :
    window(window) {
    if(TVPIsWebKirikiriZMode() && window) {
        owner = window->GetOwnerNoAddRef();
        if(owner) owner->AddRef();
    }
}

tTVPWebPointerEvent::~tTVPWebPointerEvent() {
    if(owner) owner->Release();
}

void tTVPWebPointerEvent::Deliver(Phase phase, tjs_int kind, tjs_real x,
                                 tjs_real y, tjs_real cx, tjs_real cy,
                                 tjs_uint32 shift, tjs_uint32 id) const {
    // An old Window or Layer callback may invalidate the window. Retaining the
    // dispatch prevents deletion, but an invalidated window must not receive the
    // second event.
    if(!owner || !TJSIsObjectValid(owner->IsValid(0, nullptr, nullptr, owner)))
        return;
    if(!window->CanDeliverEvents() || window->GetOwnerNoAddRef() != owner)
        return;

    static ttstr names[] = {
        ttstr(TJS_W("onPointerDown")), ttstr(TJS_W("onPointerMove")),
        ttstr(TJS_W("onPointerUp"))
    };
    tTJSVariant args[7] = {
        kind, x, y, cx, cy, (tjs_int64)shift, (tjs_int64)id
    };
    // Missing handlers follow the normal event dispatch semantics. A failed
    // FuncCall return is ignored; a thrown script exception is handled normally.
    TVPPostEvent(owner, owner, names[static_cast<unsigned>(phase)], 0,
                 TVP_EPT_IMMEDIATE, 7, args);
}
