// Host checkpoint bridge for the Web platform.
//
// This file is intentionally isolated from the reconstructed engine paths. It
// exposes the KAG bookmark API already provided by a loaded game so a browser
// host can request a semantic checkpoint without snapshotting Wasm memory.
#include "ScriptMgnIntf.h"
#include "base/CCDirector.h"
#include "base/CCScheduler.h"
#include "tjsCommHead.h"

#include <atomic>
#include <emscripten.h>

namespace {
constexpr int kBookmarkSucceeded = 0;
constexpr int kScriptUnavailable = -1;
constexpr int kKagUnavailable = -2;
constexpr int kMethodUnavailable = -3;
constexpr int kBookmarkRejected = -4;
constexpr int kScriptException = -5;
constexpr int kLoadAlreadyRequested = -6;
constexpr int kLoadIdle = 0;
constexpr int kLoadPending = 1;
constexpr int kLoadSucceeded = 2;
std::atomic<int> load_state{kLoadIdle};

bool findKagMethod(const tjs_char *name, tTJSVariant &kag_value,
                   tTJSVariant &method_value) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global ||
       global->PropGet(0, TJS_W("kag"), nullptr, &kag_value, global) !=
           TJS_S_OK ||
       kag_value.Type() != tvtObject) {
        return false;
    }
    iTJSDispatch2 *kag = kag_value.AsObjectNoAddRef();
    return kag && kag->PropGet(0, name, nullptr, &method_value, kag) ==
                      TJS_S_OK &&
           method_value.Type() == tvtObject &&
           method_value.AsObjectNoAddRef()->IsInstanceOf(
               0, nullptr, nullptr, TJS_W("Function"),
               method_value.AsObjectNoAddRef());
}

bool kagReachedSavePoint(const tTJSVariant &kag_value) {
    iTJSDispatch2 *kag = kag_value.Type() == tvtObject
                             ? kag_value.AsObjectNoAddRef()
                             : nullptr;
    tTJSVariant current_label;
    tTJSVariant in_stable;
    if(!kag || kag->PropGet(0, TJS_W("currentLabel"), nullptr,
                            &current_label, kag) != TJS_S_OK ||
       current_label.Type() != tvtString ||
       kag->PropGet(0, TJS_W("inStable"), nullptr, &in_stable, kag) !=
           TJS_S_OK) {
        return false;
    }
    auto *label = current_label.AsStringNoAddRef();
    return label && label->GetLength() > 0 && in_stable.operator bool();
}

int callKagBookmark(const tjs_char *name, tjs_int32 slot) noexcept {
    try {
        if(!TVPGetScriptDispatch()) return kScriptUnavailable;
        tTJSVariant kag_value;
        tTJSVariant method_value;
        if(!findKagMethod(name, kag_value, method_value)) {
            return kag_value.Type() == tvtObject ? kMethodUnavailable
                                                 : kKagUnavailable;
        }
        iTJSDispatch2 *kag = kag_value.AsObjectNoAddRef();
        iTJSDispatch2 *method = method_value.AsObjectNoAddRef();
        tTJSVariant result;
        tTJSVariant slot_value(slot);
        tTJSVariant *arguments = &slot_value;
        if(method->FuncCall(0, nullptr, nullptr, &result, 1, &arguments, kag) !=
           TJS_S_OK) {
            return kMethodUnavailable;
        }
        return result.operator bool() ? kBookmarkSucceeded
                                      : kBookmarkRejected;
    } catch(...) {
        return kScriptException;
    }
}

int scheduleKagLoad(tjs_int32 slot) noexcept {
    int expected = kLoadIdle;
    if(!load_state.compare_exchange_strong(expected, kLoadPending)) {
        return kLoadAlreadyRequested;
    }
    try {
        auto *director = cocos2d::Director::getInstance();
        auto *scheduler = director ? director->getScheduler() : nullptr;
        if(!scheduler) {
            load_state.store(kScriptUnavailable);
            return kScriptUnavailable;
        }
        scheduler->performFunctionInCocosThread([slot] {
            const int result = callKagBookmark(TJS_W("loadBookMark"), slot);
            load_state.store(result == kBookmarkSucceeded ? kLoadSucceeded
                                                          : result);
        });
        return kBookmarkSucceeded;
    } catch(...) {
        load_state.store(kScriptException);
        return kScriptException;
    }
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE int krkr2_host_bookmark_is_ready() {
    try {
        tTJSVariant kag_value;
        tTJSVariant save_method;
        if(!findKagMethod(TJS_W("saveBookMark"), kag_value, save_method)) {
            return 0;
        }
        tTJSVariant load_kag;
        tTJSVariant load_method;
        if(!findKagMethod(TJS_W("loadBookMark"), load_kag, load_method)) {
            return 0;
        }
        return kagReachedSavePoint(load_kag) ? 1 : 0;
    } catch(...) {
        return 0;
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE int
krkr2_host_save_bookmark(int slot) {
    return callKagBookmark(TJS_W("saveBookMark"), slot);
}

extern "C" EMSCRIPTEN_KEEPALIVE int
krkr2_host_load_bookmark(int slot) {
    return scheduleKagLoad(slot);
}

extern "C" EMSCRIPTEN_KEEPALIVE int
krkr2_host_load_bookmark_state() {
    return load_state.load();
}
