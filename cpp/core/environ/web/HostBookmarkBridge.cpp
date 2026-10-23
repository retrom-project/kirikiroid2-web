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
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>
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

struct ReleaseDispatch {
    void operator()(iTJSDispatch2 *value) const { if(value) value->Release(); }
};
using OwnedDispatch = std::unique_ptr<iTJSDispatch2, ReleaseDispatch>;
iTJSDispatch2 *retainDispatch(iTJSDispatch2 *value) {
    value->AddRef();
    return value;
}

// Some KAG overrides discard loadBookMarkFromFile's return value. Observe a
// completed restoreFlags within the requested loader call, rather than treating
// every void result (or a previously stable title screen) as a successful load.
enum class RestoreOutcome { None, Completed, Failed };

class RestoreFlagsObserver final : public tTJSDispatch {
    tTJSVariant method_;
    bool completed_ = false;
    bool failed_ = false;
    iTJSDispatch2 *receiver_ = nullptr; // Compared only while the caller owns it.

public:
    explicit RestoreFlagsObserver(const tTJSVariant &method)
        : method_(method.AsObjectNoAddRef(), nullptr) {}

    void activate(iTJSDispatch2 *receiver) { receiver_ = receiver; }
    void deactivate() { receiver_ = nullptr; }

    RestoreOutcome outcome() const {
        return failed_ ? RestoreOutcome::Failed :
            completed_ ? RestoreOutcome::Completed : RestoreOutcome::None;
    }

    tjs_error IsInstanceOf(tjs_uint32, const tjs_char *, tjs_uint32 *,
                          const tjs_char *name, iTJSDispatch2 *) override {
        return ttstr(name) == TJS_W("Function") ? TJS_S_TRUE : TJS_S_FALSE;
    }

    tjs_error FuncCall(tjs_uint32 flag, const tjs_char *name, tjs_uint32 *hint,
                      tTJSVariant *result, tjs_int count, tTJSVariant **params,
                      iTJSDispatch2 *objthis) override {
        AddRef();
        OwnedDispatch self(this);
        const bool requested = receiver_ && objthis == receiver_;
        try {
            tTJSVariant local_result;
            if(!result) result = &local_result;
            const auto status = method_.AsObjectNoAddRef()->FuncCall(
                flag, name, hint, result, count, params, objthis);
            if(requested && receiver_ == objthis) {
                if(status == TJS_S_OK && (result->Type() == tvtVoid || result->operator bool()))
                    completed_ = true;
                else failed_ = true;
            }
            return status;
        } catch(...) {
            if(requested && receiver_ == objthis) failed_ = true;
            throw;
        }
    }
};

class ScopedRestoreWatch final {
    iTJSDispatch2 *kag_;
    tTJSVariant original_;
    OwnedDispatch observer_;
    bool detached_ = false;

public:
    explicit ScopedRestoreWatch(iTJSDispatch2 *kag) : kag_(kag) {}

    bool install() {
        if(kag_->PropGet(0, TJS_W("restoreFlags"), nullptr, &original_, kag_) != TJS_S_OK ||
           original_.Type() != tvtObject || !original_.AsObjectNoAddRef() ||
           original_.AsObjectNoAddRef()->IsInstanceOf(
               0, nullptr, nullptr, TJS_W("Function"), kag_) != TJS_S_TRUE)
            return true; // An absent method supplies no completion witness.
        observer_.reset(new RestoreFlagsObserver(original_));
        const tTJSVariant callback(observer_.get(), nullptr);
        return kag_->PropSet(TJS_MEMBERENSURE, TJS_W("restoreFlags"), nullptr,
                             &callback, kag_) == TJS_S_OK;
    }

    RestoreOutcome outcome() const {
        return observer_ ? static_cast<RestoreFlagsObserver *>(observer_.get())->outcome()
                         : RestoreOutcome::None;
    }

    void activate() {
        if(observer_) static_cast<RestoreFlagsObserver *>(observer_.get())->activate(kag_);
    }

    void deactivate() {
        if(observer_) static_cast<RestoreFlagsObserver *>(observer_.get())->deactivate();
    }

    bool detach() noexcept {
        if(!observer_ || detached_) return true;
        try {
            tTJSVariant current;
            if(kag_->PropGet(0, TJS_W("restoreFlags"), nullptr, &current, kag_) != TJS_S_OK)
                return false;
            // Keep replacements installed by the script, including on failure.
            if(current.Type() != tvtObject || current.AsObjectNoAddRef() != observer_.get())
                return detached_ = true;
            detached_ = kag_->PropSet(TJS_MEMBERENSURE, TJS_W("restoreFlags"), nullptr,
                                     &original_, kag_) == TJS_S_OK;
            return detached_;
        } catch(...) {
            return false;
        }
    }

    ~ScopedRestoreWatch() {
        deactivate();
        if(!detach()) std::fprintf(stderr, "[bookmark] cannot detach restore observer\n");
    }
};

tjs_error invokeBookmarkLoad(const tTJSVariant &method, tjs_uint32 flag,
                            const tjs_char *name, tjs_uint32 *hint,
                            tTJSVariant *result, tjs_int count,
                            tTJSVariant **params, iTJSDispatch2 *kag,
                            RestoreOutcome &restored) {
    ScopedRestoreWatch watch(kag);
    if(!watch.install()) return TJS_E_FAIL;
    watch.activate();
    tjs_error status;
    try {
        status = method.AsObjectNoAddRef()->FuncCall(
            flag, name, hint, result, count, params, kag);
    } catch(...) {
        watch.deactivate();
        throw;
    }
    watch.deactivate();
    // The loader can still fail after restoreFlags. Publish no result before
    // the entire original call has returned and the temporary hook is removed.
    if(!watch.detach()) return TJS_E_FAIL;
    restored = watch.outcome();
    return status;
}

int bookmarkLoadOutcome(tjs_error status, const tTJSVariant &result, RestoreOutcome restored) {
    if(status != TJS_S_OK) return kMethodUnavailable;
    if(restored == RestoreOutcome::Failed) return kBookmarkRejected;
    if(result.Type() == tvtVoid)
        return restored == RestoreOutcome::Completed ? kLoadSucceeded : kBookmarkRejected;
    return result.operator bool() ? kLoadSucceeded : kBookmarkRejected;
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
        // Capture needs a savepoint. The restore coordinator has already let
        // first-scenario initialization reach a load boundary within engine ticks.
        if(capture && !kagReachedSavePoint(kag_value)) return kBookmarkRejected;
        iTJSDispatch2 *kag = kag_value.AsObjectNoAddRef();
        iTJSDispatch2 *method = method_value.AsObjectNoAddRef();
        tTJSVariant result;
        tTJSVariant slot_value(slot);
        tTJSVariant *arguments = &slot_value;
        if(!capture) {
            RestoreOutcome restored = RestoreOutcome::None;
            const auto status = invokeBookmarkLoad(method_value, 0, nullptr, nullptr,
                &result, 1, &arguments, kag, restored);
            const auto outcome = bookmarkLoadOutcome(status, result, restored);
            return outcome == kLoadSucceeded ? kBookmarkSucceeded : outcome;
        }
        if(method->FuncCall(0, nullptr, nullptr, &result, 1, &arguments, kag) != TJS_S_OK) {
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
            pending_load->observer.get() == this &&
            pending_load->kag.get() == objthis && count >= 1 && params &&
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
            RestoreOutcome restored = RestoreOutcome::None;
            const auto status = owner
                ? invokeBookmarkLoad(method_, flag, name, hint, result, count,
                                     params, objthis, restored)
                : method_.AsObjectNoAddRef()->FuncCall(
                    flag, name, hint, result, count, params, objthis);
            if(owner) {
                load_state.store(bookmarkLoadOutcome(status, *result, restored));
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

// The loader is published before a KAG first scenario has registered all its
// macros/plugins. A host restore must finish that bootstrap before replacing
// the scenario. This is Web host behavior, not a reconstructed native API.
struct BootstrapWaitPoint {
    tTJSVariant conductor;
    ttstr storage;
    tjs_int32 line;
    tjs_int32 depth;

    bool matches(const BootstrapWaitPoint &other) const {
        return conductor.AsObjectNoAddRef() == other.conductor.AsObjectNoAddRef() &&
               storage == other.storage && line == other.line && depth == other.depth;
    }
};

struct BootstrapLoad {
    std::uint64_t id;
    tjs_int32 slot;
    tTJSVariant kag;
    std::vector<BootstrapWaitPoint> acknowledged;
};
std::shared_ptr<BootstrapLoad> bootstrap_load;
std::uint64_t bootstrap_sequence = 0;
constexpr std::size_t kMaxBootstrapSignals = 32;

bool readProperty(iTJSDispatch2 *object, const tjs_char *name, tTJSVariant &value) {
    return object && object->PropGet(0, name, nullptr, &value, object) == TJS_S_OK;
}

bool readInteger(iTJSDispatch2 *object, const tjs_char *name, tjs_int32 &value) {
    tTJSVariant property;
    if(!readProperty(object, name, property) || property.Type() != tvtInteger) return false;
    value = static_cast<tjs_int32>(property);
    return true;
}

bool isCallable(const tTJSVariant &value) {
    return value.Type() == tvtObject && value.AsObjectNoAddRef() &&
           value.AsObjectNoAddRef()->IsInstanceOf(
               0, nullptr, nullptr, TJS_W("Function"), value.AsObjectNoAddRef()) == TJS_S_TRUE;
}

enum class BootstrapAction { Wait, Ready, Click };

BootstrapAction inspectBootstrap(iTJSDispatch2 *kag, BootstrapWaitPoint &point,
                                  tTJSVariant &trigger) {
    tjs_int32 stable, first, status, running, waiting;
    if(!readInteger(kag, TJS_W("inStable"), stable)) return BootstrapAction::Wait;
    tTJSVariant conductor;
    readProperty(kag, TJS_W("conductor"), conductor);
    auto *current = conductor.Type() == tvtObject ? conductor.AsObjectNoAddRef() : nullptr;
    const bool has_status = readInteger(current, TJS_W("status"), status);
    const bool is_running = has_status && readInteger(current, TJS_W("mRun"), running) &&
                            status == running;
    if(is_running) return BootstrapAction::Wait;
    if(stable) return BootstrapAction::Ready;

    // Plain KAG can await host input before starting its first scenario at all.
    // It has no initialization process to interrupt, unlike an active conductor.
    tjs_int32 stopped;
    if(readInteger(kag, TJS_W("isFirstProcess"), first) && first &&
       (!current || (has_status && readInteger(current, TJS_W("mStop"), stopped) &&
                     status == stopped))) return BootstrapAction::Ready;

    tTJSVariant main, tag, waits, click, timer, storage;
    tjs_int32 enabled, processing, line, depth;
    tjs_int count;
    if(!current || !has_status || !readInteger(current, TJS_W("mWait"), waiting) || status != waiting ||
       !readInteger(current, TJS_W("timerEnabled"), processing) || processing ||
       !readProperty(kag, TJS_W("mainConductor"), main) || main.Type() != tvtObject ||
       main.AsObjectNoAddRef() != current ||
       !readProperty(current, TJS_W("lastTagName"), tag) || tag.Type() != tvtString ||
       !tag.AsStringNoAddRef() ||
       ttstr(static_cast<const tjs_char *>(*tag.AsStringNoAddRef())) != TJS_W("waitclick") ||
       !readProperty(current, TJS_W("waitUntil"), waits) || waits.Type() != tvtObject ||
       !waits.AsObjectNoAddRef() ||
       waits.AsObjectNoAddRef()->GetCount(&count, nullptr, nullptr, waits.AsObjectNoAddRef()) != TJS_S_OK || count != 1 ||
       !readProperty(waits.AsObjectNoAddRef(), TJS_W("click"), click) || !isCallable(click) ||
       !readProperty(current, TJS_W("timeOutTimer"), timer) || timer.Type() != tvtObject ||
       !readInteger(timer.AsObjectNoAddRef(), TJS_W("enabled"), enabled) || enabled ||
       !readProperty(current, TJS_W("trigger"), trigger) || !isCallable(trigger) ||
       !readProperty(current, TJS_W("curStorage"), storage) || storage.Type() != tvtString ||
       !storage.AsStringNoAddRef() || !storage.AsStringNoAddRef()->GetLength() ||
       !readInteger(current, TJS_W("curLine"), line) || line < 0 ||
       !readInteger(current, TJS_W("callStackDepth"), depth) || depth < 0)
        return BootstrapAction::Wait;
    point = {conductor, ttstr(static_cast<const tjs_char *>(*storage.AsStringNoAddRef())), line, depth};
    return BootstrapAction::Click;
}

bool bootstrapIsCurrent(const std::shared_ptr<BootstrapLoad> &request) {
    return bootstrap_load == request && load_state.load() == kLoadPending;
}

void pollBootstrap(std::uint64_t id);

bool queueBootstrapPoll(std::uint64_t id) {
    auto *director = cocos2d::Director::getInstance();
    auto *scheduler = director ? director->getScheduler() : nullptr;
    if(!scheduler) return false;
    // The queue captures only an identity, so cancellation releases all script
    // references before engine teardown even if this callback is never delivered.
    scheduler->performFunctionInCocosThread([id] { pollBootstrap(id); });
    return true;
}

void pollBootstrap(std::uint64_t id) {
    auto request = bootstrap_load;
    if(!request || request->id != id || !bootstrapIsCurrent(request)) return;
    const auto fail = [&](int outcome) {
        if(bootstrapIsCurrent(request)) {
            load_state.store(outcome);
            bootstrap_load.reset();
        }
    };
    try {
        tTJSVariant kag, method, trigger;
        if(!findKagMethod(TJS_W("loadBookMark"), kag, method)) {
            fail(kMethodUnavailable);
            return;
        }
        if(!bootstrapIsCurrent(request)) return;
        if(request->kag.Type() == tvtVoid) request->kag = kag;
        if(request->kag.AsObjectNoAddRef() != kag.AsObjectNoAddRef()) {
            fail(kBookmarkRejected);
            return;
        }
        BootstrapWaitPoint point;
        const auto action = inspectBootstrap(kag.AsObjectNoAddRef(), point, trigger);
        if(!bootstrapIsCurrent(request)) return;
        if(action == BootstrapAction::Ready) {
            bootstrap_load.reset();
            const int outcome = startKagLoad(request->slot);
            if(outcome != kBookmarkSucceeded) load_state.store(outcome);
            return;
        }
        if(action == BootstrapAction::Click) {
            bool seen = request->acknowledged.size() >= kMaxBootstrapSignals;
            for(const auto &previous : request->acknowledged) seen |= previous.matches(point);
            if(!seen) {
                // Record before invoking script; never retry a failed or reentrant signal.
                request->acknowledged.push_back(point);
                tTJSVariant result, signal(TJS_W("click"));
                tTJSVariant *argument = &signal;
                const auto status = trigger.AsObjectNoAddRef()->FuncCall(
                    0, nullptr, nullptr, &result, 1, &argument, point.conductor.AsObjectNoAddRef());
                if(!bootstrapIsCurrent(request)) return;
                if(status != TJS_S_OK || result.Type() != tvtInteger || !result.operator bool()) {
                    fail(kBookmarkRejected);
                    return;
                }
                std::fprintf(stderr, "[bookmark] bootstrap click acknowledged (%zu/%zu)\n",
                             request->acknowledged.size(), kMaxBootstrapSignals);
            }
        }
        // No spinning, synthetic DOM input, menu choices, or timed-wait bypass:
        // one standard waitclick signal at most, then let the next tick advance.
        if(bootstrapIsCurrent(request) && !queueBootstrapPoll(id)) fail(kScriptUnavailable);
    } catch(...) {
        fail(kScriptException);
    }
}

int scheduleKagLoad(tjs_int32 slot) noexcept {
    if(!TVPStartupSuccess || !krkr2_host_main_loop_is_idle())
        return kBookmarkRejected;
    int expected = kLoadIdle;
    if(!load_state.compare_exchange_strong(expected, kLoadPending))
        return kLoadAlreadyRequested;
    try {
        bootstrap_load = std::make_shared<BootstrapLoad>();
        bootstrap_load->id = ++bootstrap_sequence;
        bootstrap_load->slot = slot;
        if(!queueBootstrapPoll(bootstrap_load->id)) {
            bootstrap_load.reset();
            load_state.store(kScriptUnavailable);
            return kScriptUnavailable;
        }
        return kBookmarkSucceeded;
    } catch(...) {
        bootstrap_load.reset();
        load_state.store(kScriptException);
        return kScriptException;
    }
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE int krkr2_host_load_bookmark_is_ready() {
    // Startup success is set only after startup.tjs / Initialize.tjs returns.
    // Loader availability is meaningful only between complete engine ticks.
    // This query only reports whether a request can be queued. The queued
    // coordinator advances standard startup barriers before invoking the loader.
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
    if(load_state.load() == kLoadPending) load_state.store(kBookmarkRejected);
    bootstrap_load.reset();
    pending_load.reset();
}
