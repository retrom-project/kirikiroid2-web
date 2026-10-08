// Host checkpoint bridge for the Web platform.
//
// This file is intentionally isolated from the reconstructed engine paths. It
// exposes the KAG bookmark API already provided by a loaded game so a browser
// host can request a semantic checkpoint without snapshotting Wasm memory.
#include "ScriptMgnIntf.h"
#include "EventIntf.h"
#include "tjsObject.h"
#include "tjsCommHead.h"

#include <atomic>
#include <cstdio>
#include <exception>
#include <string>
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

class ScriptDispatch final {
public:
    iTJSDispatch2 *const value = TVPGetScriptDispatch();
    ScriptDispatch() = default;
    ScriptDispatch(const ScriptDispatch &) = delete;
    ScriptDispatch &operator=(const ScriptDispatch &) = delete;
    ~ScriptDispatch() {
        if(value) value->Release();
    }
};

#ifdef __EMSCRIPTEN__
EM_JS(int, trace_bookmarks_enabled, (), {
    return Module['krkr2TraceBookmarks'] ? 1 : 0;
});

void traceBookmarkState(const char *stage) noexcept {
    if(!trace_bookmarks_enabled()) return;
    try {
        auto read = [](iTJSDispatch2 *object, const char *name) {
            tTJSVariant value;
            if(object) {
                try {
                    const ttstr key(name);
                    object->PropGet(0, key.c_str(), nullptr, &value, object);
                } catch(...) {}
            }
            return value;
        };
        auto object = [](const tTJSVariant &value) {
            return value.Type() == tvtObject ? value.AsObjectNoAddRef() : nullptr;
        };
        ScriptDispatch global;
        tTJSVariant kag_value = read(global.value, "kag");
        auto *kag = object(kag_value);
        std::string trace;
        auto fields = [&](const char *prefix, iTJSDispatch2 *target,
                          std::initializer_list<const char *> names) {
            trace += std::string(prefix) + "@" + std::to_string(reinterpret_cast<uintptr_t>(target)) + "{";
            for(auto name : names) {
                const auto value = read(target, name);
                trace += std::string(name) + ":";
                if(value.Type() == tvtObject)
                    trace += "@" + std::to_string(reinterpret_cast<uintptr_t>(object(value)));
                else
                    trace += ttstr(value).AsStdString();
                trace += ",";
            }
            trace += "}";
        };
        fields("kag", kag, {"currentStorage", "currentLabel", "inStable", "isFirstProcess", "currentPage", "currentNum", "inSleep", "inTransition", "inFlipInterval", "flipStartFlag", "transShowing", "visible", "inShow", "usingExtraConductor", "_clickWaiting", "isWaitPeriodEvent", "holdPeriodEventQueue"});
        auto can_restore = read(kag, "canRestore");
        if(auto *method = object(can_restore)) {
            tTJSVariant result;
            method->FuncCall(0, nullptr, nullptr, &result, 0, nullptr, kag);
            trace += "canRestore:" + ttstr(result).AsStdString();
        }
        for(auto name : {"conductor", "mainConductor", "extraConductor"}) {
            auto conductor = read(kag, name);
            fields(name, object(conductor), {"status", "curStorage", "curLine", "enabled", "interval", "timer", "oneshot", "oneShot", "tickCount"});
            auto timer = read(object(conductor), "timer");
            fields("timer", object(timer), {"enabled", "interval"});
        }
        auto layer = [&](const char *name, const tTJSVariant &value) {
            fields(name, object(value), {"visible", "opacity", "hasImage", "imageWidth", "imageHeight", "width", "height", "left", "top", "type", "parent", "absolute", "imageLeft", "imageTop", "drawPlane"});
        };
        for(auto name : {"_primaryLayer", "sysbase", "uibase", "btLayer", "_transLayer", "current", "snapshotLayer", "_sysCoverLayer"})
            layer(name, read(kag, name));
        for(auto name : {"fore", "back"}) {
            auto page = read(kag, name);
            layer(name, read(object(page), "base"));
        }
        static std::string last;
        if(trace != last || std::string(stage) != "poll") {
            last = trace;
            std::fprintf(stderr, "[bookmark-trace] %s %s\n", stage, trace.c_str());
        }
    } catch(...) {
        std::fprintf(stderr, "[bookmark-trace] observation failed\n");
    }
}
#else
void traceBookmarkState(const char *) noexcept {}
#endif

bool findKagMethod(const tjs_char *name, tTJSVariant &kag_value,
                   tTJSVariant &method_value) {
    ScriptDispatch script;
    iTJSDispatch2 *global = script.value;
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
        ScriptDispatch script;
        if(!script.value) return kScriptUnavailable;
        tTJSVariant kag_value;
        tTJSVariant method_value;
        if(!findKagMethod(name, kag_value, method_value)) {
            return kag_value.Type() == tvtObject ? kMethodUnavailable
                                                 : kKagUnavailable;
        }
        // Capturing needs a script savepoint. Loading can replace a startup
        // wait that never notifies KAG stable; it was posted only after the
        // initialization tick returned, and now runs as a native idle event.
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

#ifdef __EMSCRIPTEN__
EM_JS(int, normal_load_probe_enabled, (), {
    return Module['krkr2NormalLoadProbe'] ? 1 : 0;
});

class BookmarkLoadObserver final : public tTJSDispatch {
    iTJSDispatch2 *kag_;
    tTJSVariant method_;

public:
    BookmarkLoadObserver(iTJSDispatch2 *kag, const tTJSVariant &method)
        : kag_(kag), method_(method) {}

    tjs_error IsInstanceOf(tjs_uint32, const tjs_char *, tjs_uint32 *,
                           const tjs_char *name, iTJSDispatch2 *) override {
        return ttstr(name) == TJS_W("Function") ? TJS_S_TRUE : TJS_S_FALSE;
    }

    void restore() {
        kag_->PropSet(TJS_MEMBERENSURE, TJS_W("loadBookMark"), nullptr,
                       &method_, kag_);
    }

    tjs_error FuncCall(tjs_uint32 flag, const tjs_char *name, tjs_uint32 *hint,
                       tTJSVariant *result, tjs_int count, tTJSVariant **params,
                       iTJSDispatch2 *objthis) override {
        AddRef();
        try {
            restore();
            tTJSVariant local_result;
            if(!result) result = &local_result;
            std::fprintf(stderr, "[bookmark] framework invoked original loader\n");
            const auto status = method_.AsObjectNoAddRef()->FuncCall(
                flag, name, hint, result, count, params, objthis);
            load_state.store(status == TJS_S_OK && result->operator bool()
                                 ? kLoadSucceeded : kBookmarkRejected);
            std::fprintf(stderr, "[bookmark] original loader completed=%d\n", load_state.load());
            Release();
            return status;
        } catch(...) {
            load_state.store(kScriptException);
            Release();
            throw;
        }
    }
};

void probeNormalLoad(tjs_int32 slot) {
    ScriptDispatch global;
    tTJSVariant kag_value, method, entry;
    if(!global.value || !findKagMethod(TJS_W("loadBookMark"), kag_value, method) ||
       global.value->PropGet(0, TJS_W("loadFunction"), nullptr, &entry,
                             global.value) != TJS_S_OK || entry.Type() != tvtObject) {
        load_state.store(kMethodUnavailable);
        return;
    }
    auto *kag = kag_value.AsObjectNoAddRef();
    auto *observer = new BookmarkLoadObserver(kag, method);
    try {
        tTJSVariant wrapped(observer, nullptr);
        kag->PropSet(TJS_MEMBERENSURE, TJS_W("loadBookMark"), nullptr, &wrapped, kag);
        tTJSVariant slot_value(slot), result;
        tTJSVariant *argument = &slot_value;
        const auto status = entry.AsObjectNoAddRef()->FuncCall(
            0, nullptr, nullptr, &result, 1, &argument, global.value);
        if(status != TJS_S_OK) {
            observer->restore();
            load_state.store(kMethodUnavailable);
        }
        observer->Release();
    } catch(...) {
        observer->restore();
        observer->Release();
        load_state.store(kScriptException);
        throw;
    }
}
#endif

class LoadBookmarkEvent final : public tTJSDispatch {
    tjs_int32 slot_;

public:
    explicit LoadBookmarkEvent(tjs_int32 slot) : slot_(slot) {}

    tjs_error IsValid(tjs_uint32, const tjs_char *, tjs_uint32 *,
                      iTJSDispatch2 *) override {
        return TJS_S_TRUE;
    }

    tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                       tTJSVariant *, tjs_int, tTJSVariant **,
                       iTJSDispatch2 *) override {
        std::fprintf(stderr, "[bookmark] native idle event dispatch\n");
        traceBookmarkState("before-load");
#ifdef __EMSCRIPTEN__
        if(normal_load_probe_enabled()) {
            probeNormalLoad(slot_);
            return TJS_S_OK;
        }
#endif
        const int result = callKagBookmark(TJS_W("loadBookMark"), slot_, false);
        traceBookmarkState("after-load");
        load_state.store(result == kBookmarkSucceeded ? kLoadSucceeded : result);
        return TJS_S_OK;
    }
};

int scheduleKagLoad(tjs_int32 slot) noexcept {
    if(!TVPStartupSuccess || !krkr2_host_main_loop_is_idle())
        return kBookmarkRejected;
    int expected = kLoadIdle;
    if(!load_state.compare_exchange_strong(expected, kLoadPending)) {
        return kLoadAlreadyRequested;
    }
    LoadBookmarkEvent *event = nullptr;
    try {
        event = new LoadBookmarkEvent(slot);
        ttstr name(TJS_W("loadBookMark"));
        // Loading is a script event. Let startup input/normal events finish
        // before the host load, instead of calling into TJS from the Cocos
        // scheduler before the engine's own event delivery phase.
        TVPPostEvent(event, event, name, 0, TVP_EPT_POST | TVP_EPT_IDLE,
                     0, nullptr);
        event->Release();
        return kBookmarkSucceeded;
    } catch(...) {
        if(event) event->Release();
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
    traceBookmarkState("poll");
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
