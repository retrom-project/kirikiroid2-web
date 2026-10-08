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
#include <cstdio>
#include <exception>
#include <emscripten.h>

// Host queries run outside the engine tick. JSPI can suspend a constructor
// after publishing global.kag but before its layers and methods are complete.
// Never inspect that partially constructed TJS object until the tick returns.
EM_JS(int, krkr2_host_main_loop_is_idle, (), {
    var state = globalThis.__krkr2MainLoopPromiseState;
    return state && !state.pending && !state.stopping ? 1 : 0;
});

extern bool TVPStartupSuccess;

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
           method_value.AsObjectNoAddRef() &&
           method_value.AsObjectNoAddRef()->IsInstanceOf(
               0, nullptr, nullptr, TJS_W("Function"),
               method_value.AsObjectNoAddRef()) == TJS_S_TRUE;
}

bool kagIsStable(const tTJSVariant &kag_value) {
    iTJSDispatch2 *kag = kag_value.Type() == tvtObject
                             ? kag_value.AsObjectNoAddRef()
                             : nullptr;
    tTJSVariant in_stable;
    return kag &&
           kag->PropGet(0, TJS_W("inStable"), nullptr, &in_stable, kag) == TJS_S_OK &&
           in_stable.Type() == tvtInteger && in_stable.operator bool();
}

bool kagReachedSavePoint(const tTJSVariant &kag_value) {
    if(!kagIsStable(kag_value)) return false;
    iTJSDispatch2 *kag = kag_value.AsObjectNoAddRef();
    tTJSVariant current_label;
    if(kag->PropGet(0, TJS_W("currentLabel"), nullptr,
                            &current_label, kag) != TJS_S_OK ||
       current_label.Type() != tvtString) {
        return false;
    }
    auto *label = current_label.AsStringNoAddRef();
    return label && label->GetLength() > 0;
}

int callKagBookmark(const tjs_char *name, tjs_int32 slot, bool capture) noexcept {
    try {
        if(!TVPGetScriptDispatch()) return kScriptUnavailable;
        tTJSVariant kag_value;
        tTJSVariant method_value;
        if(!findKagMethod(name, kag_value, method_value)) {
            return kag_value.Type() == tvtObject ? kMethodUnavailable
                                                 : kKagUnavailable;
        }
        // Capturing needs a script savepoint. Loading can replace a startup
        // wait that never notifies KAG stable; it was queued only after the
        // initialization tick returned, and now runs within its own tick.
        if(capture && !kagReachedSavePoint(kag_value)) return kBookmarkRejected;
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
    } catch(const std::exception &error) {
        std::fprintf(stderr, "[bookmark] script exception: %s\n", error.what());
        return kScriptException;
    } catch(...) {
        std::fprintf(stderr, "[bookmark] unknown script exception\n");
        return kScriptException;
    }
}

int scheduleKagLoad(tjs_int32 slot) noexcept {
    if(!TVPStartupSuccess || !krkr2_host_main_loop_is_idle())
        return kBookmarkRejected;
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
            const int result = callKagBookmark(TJS_W("loadBookMark"), slot, false);
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

extern "C" EMSCRIPTEN_KEEPALIVE int krkr2_host_load_bookmark_is_ready() {
    static int observed_startup = -1;
    if(observed_startup != static_cast<int>(TVPStartupSuccess)) {
        observed_startup = TVPStartupSuccess;
        std::fprintf(stderr, "[bookmark] startup completed=%d, idle=%d\n",
                     observed_startup, krkr2_host_main_loop_is_idle());
    }
    // Startup success is set only after startup.tjs / Initialize.tjs returns.
    // Loader availability is meaningful only between complete engine ticks.
    // Script stable/save-label flags can require input during startup, so they
    // belong to capture readiness, not to loading an existing bookmark.
    if(!TVPStartupSuccess || !krkr2_host_main_loop_is_idle()) return 0;
    try {
        tTJSVariant kag_value;
        tTJSVariant load_method;
        return findKagMethod(TJS_W("loadBookMark"), kag_value, load_method)
                   ? 1
                   : 0;
    } catch(...) {
        return 0;
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE int krkr2_host_bookmark_is_ready() {
    if(!TVPStartupSuccess || !krkr2_host_main_loop_is_idle()) return 0;
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
    if(!krkr2_host_main_loop_is_idle()) return kBookmarkRejected;
    return callKagBookmark(TJS_W("saveBookMark"), slot, true);
}

extern "C" EMSCRIPTEN_KEEPALIVE int
krkr2_host_load_bookmark(int slot) {
    return scheduleKagLoad(slot);
}

extern "C" EMSCRIPTEN_KEEPALIVE int
krkr2_host_load_bookmark_state() {
    return load_state.load();
}
