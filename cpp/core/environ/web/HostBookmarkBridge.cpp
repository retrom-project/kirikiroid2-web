// Host checkpoint bridge for the Web platform.
//
// This file is intentionally isolated from the reconstructed engine paths. It
// exposes the KAG bookmark API already provided by a loaded game so a browser
// host can request a semantic checkpoint without snapshotting Wasm memory.
#include "ScriptMgnIntf.h"
#include "base/CCDirector.h"
#include "base/CCScheduler.h"
#include "tjsObject.h"
#include "tjsCommHead.h"

#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
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
        // initialization tick returned, and now runs within its own engine tick.
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

struct ReleaseDispatch {
    void operator()(iTJSDispatch2 *value) const { if(value) value->Release(); }
};
using OwnedDispatch = std::unique_ptr<iTJSDispatch2, ReleaseDispatch>;
iTJSDispatch2 *retainDispatch(iTJSDispatch2 *value) {
    value->AddRef();
    return value;
}

// Explicit ownership covers an asynchronous framework load. The observer holds
// only the function, so failed detachment cannot create a receiver/reference cycle.
struct PendingLoad {
    OwnedDispatch kag;
    tTJSVariant original;
    OwnedDispatch observer;

    PendingLoad(iTJSDispatch2 *receiver, const tTJSVariant &method,
                 iTJSDispatch2 *callback)
        : kag(retainDispatch(receiver)), original(method),
          observer(retainDispatch(callback)) {}

    bool detach() noexcept {
        try {
            tTJSVariant current;
            if(kag->PropGet(0, TJS_W("loadBookMark"), nullptr,
                            &current, kag.get()) != TJS_S_OK)
                return false;
            // Do not overwrite a replacement installed by the game.
            if(current.Type() != tvtObject ||
               current.AsObjectNoAddRef() != observer.get()) return true;
            return kag->PropSet(TJS_MEMBERENSURE, TJS_W("loadBookMark"), nullptr,
                                &original, kag.get()) == TJS_S_OK;
        } catch(...) {
            return false;
        }
    }

    ~PendingLoad() {
        if(!detach())
            std::fprintf(stderr, "[bookmark] cannot detach load observer\n");
    }
};
std::unique_ptr<PendingLoad> pending_load;

class BookmarkLoadObserver final : public tTJSDispatch {
    tTJSVariant method_;
    tjs_int32 slot_;

public:
    BookmarkLoadObserver(const tTJSVariant &method, tjs_int32 slot)
        : method_(method.AsObjectNoAddRef(), nullptr), slot_(slot) {}

    tjs_error IsInstanceOf(tjs_uint32, const tjs_char *, tjs_uint32 *,
                           const tjs_char *name, iTJSDispatch2 *) override {
        return ttstr(name) == TJS_W("Function") ? TJS_S_TRUE : TJS_S_FALSE;
    }

    tjs_error FuncCall(tjs_uint32 flag, const tjs_char *name, tjs_uint32 *hint,
                       tTJSVariant *result, tjs_int count, tTJSVariant **params,
                       iTJSDispatch2 *objthis) override {
        AddRef();
        OwnedDispatch self(this);
        const bool requested = pending_load &&
            pending_load->observer.get() == this && count >= 1 && params &&
            params[0] && params[0]->Type() == tvtInteger &&
            static_cast<tjs_int32>(*params[0]) == slot_;
        auto owner = requested ? std::move(pending_load) : nullptr;
        if(owner && !owner->detach()) {
            load_state.store(kScriptException);
            return TJS_E_FAIL;
        }
        try {
            tTJSVariant local_result;
            if(!result) result = &local_result;
            const auto status = method_.AsObjectNoAddRef()->FuncCall(
                flag, name, hint, result, count, params, objthis);
            if(owner) {
                load_state.store(status != TJS_S_OK ? kMethodUnavailable :
                    result->operator bool() ? kLoadSucceeded : kBookmarkRejected);
            }
            return status;
        } catch(const std::exception &error) {
            if(owner) {
                load_state.store(kScriptException);
                std::fprintf(stderr, "[bookmark] script exception: %s\n", error.what());
            }
            throw;
        } catch(...) {
            if(owner) {
                load_state.store(kScriptException);
                std::fprintf(stderr, "[bookmark] unknown script exception\n");
            }
            throw;
        }
    }
};

int startKagLoad(tjs_int32 slot) noexcept {
    try {
        ScriptDispatch global;
        tTJSVariant kag, method, entry;
        if(!global.value) return kScriptUnavailable;
        if(!findKagMethod(TJS_W("loadBookMark"), kag, method))
            return kag.Type() == tvtObject ? kMethodUnavailable : kKagUnavailable;

        // KAGEX's public loadFunction(slot) performs framework loadinit before
        // calling the KAG bookmark method. Plain KAG has no framework entry.
        // Reference: krkrz/krkr2@dec49af97, kag3ex3/data/main/Override.tjs.
        // Never bypass a present framework entry after rejection or failure.
        if(global.value->PropGet(0, TJS_W("loadFunction"), nullptr, &entry,
                                  global.value) != TJS_S_OK || entry.Type() == tvtVoid) {
            const int outcome = callKagBookmark(TJS_W("loadBookMark"), slot, false);
            load_state.store(outcome == kBookmarkSucceeded ? kLoadSucceeded : outcome);
            return outcome;
        }
        if(entry.Type() != tvtObject || !entry.AsObjectNoAddRef() ||
           entry.AsObjectNoAddRef()->IsInstanceOf(0, nullptr, nullptr,
               TJS_W("Function"), entry.AsObjectNoAddRef()) != TJS_S_TRUE)
            return kMethodUnavailable;

        OwnedDispatch observer(new BookmarkLoadObserver(method, slot));
        pending_load = std::make_unique<PendingLoad>(
            kag.AsObjectNoAddRef(), method, observer.get());
        tTJSVariant wrapped(observer.get(), nullptr);
        if(kag.AsObjectNoAddRef()->PropSet(TJS_MEMBERENSURE, TJS_W("loadBookMark"),
            nullptr, &wrapped, kag.AsObjectNoAddRef()) != TJS_S_OK) {
            pending_load.reset();
            return kMethodUnavailable;
        }
        tTJSVariant result, slot_value(slot);
        tTJSVariant *argument = &slot_value;
        if(entry.AsObjectNoAddRef()->FuncCall(0, nullptr, nullptr, &result, 1,
                                             &argument, global.value) != TJS_S_OK) {
            pending_load.reset();
            return kMethodUnavailable;
        }
        // The entry can return before loading, and need not return a value.
        // Only its original bookmark call completes the host operation.
        return kBookmarkSucceeded;
    } catch(const std::exception &error) {
        pending_load.reset();
        std::fprintf(stderr, "[bookmark] script exception: %s\n", error.what());
        return kScriptException;
    } catch(...) {
        pending_load.reset();
        std::fprintf(stderr, "[bookmark] unknown script exception\n");
        return kScriptException;
    }
}

int scheduleKagLoad(tjs_int32 slot) noexcept {
    if(!TVPStartupSuccess || !krkr2_host_main_loop_is_idle())
        return kBookmarkRejected;
    int expected = kLoadIdle;
    if(!load_state.compare_exchange_strong(expected, kLoadPending))
        return kLoadAlreadyRequested;
    try {
        auto *director = cocos2d::Director::getInstance();
        auto *scheduler = director ? director->getScheduler() : nullptr;
        if(!scheduler) {
            load_state.store(kScriptUnavailable);
            return kScriptUnavailable;
        }
        scheduler->performFunctionInCocosThread([slot] {
            if(load_state.load() != kLoadPending) return;
            const int outcome = startKagLoad(slot);
            if(outcome != kBookmarkSucceeded) load_state.store(outcome);
        });
        return kBookmarkSucceeded;
    } catch(...) {
        load_state.store(kScriptException);
        return kScriptException;
    }
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE int krkr2_host_load_bookmark_is_ready() {
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

// Stop calls this after the JSPI stack drains, while the TJS objects are alive.
extern "C" EMSCRIPTEN_KEEPALIVE void krkr2_host_cancel_bookmark_load() {
    pending_load.reset();
    if(load_state.load() == kLoadPending) load_state.store(kBookmarkRejected);
}
