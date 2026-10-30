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
#include "SysInitIntf.h"
#include "HostNativeExit.h"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"

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

// This is an observation of the game's original save call, not a request to
// save or load. Host callbacks are synchronous notifications and must not
// replace an original script result or exception with a JavaScript failure.
EM_JS(void, krkr2_host_notify_game_save, (int phase, int id, int success, int history), {
    try {
        var callback = Module['onGameSave'];
        if (typeof callback === 'function') {
            callback(phase === 0 ? {phase: 'begin', id: id, history: history !== 0}
                                 : {phase: 'end', id: id, success: success !== 0});
        }
    } catch (error) {
        console.warn('[game-save] Host notification failed', error);
    }
});

// The game's own representative image of a player save point, as RGBA rows.
EM_JS(void, krkr2_host_publish_game_save_image, (int id, const unsigned char *pixels,
                                                 int width, int height), {
    try {
        var callback = Module['onGameSaveImage'];
        if (typeof callback === 'function') {
            callback({id: id, width: width, height: height,
                      pixels: HEAPU8.slice(pixels, pixels + width * height * 4)});
        }
    } catch (error) {
        console.warn('[game-save] Host image notification failed', error);
    }
});

extern bool TVPStartupSuccess;

EM_JS(void, krkr2_host_notify_native_exit, (int status), {
    try {
        var callback = Module['onNativeExitRequested'];
        if (typeof callback === 'function') callback(status);
    } catch (error) {
        console.warn('[native-exit] Host notification failed', error);
    }
});

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
bool native_exit_requested = false;
bool native_exit_completed = false;
int native_exit_status = 0;

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

// Explicit Web GAME_SAVE boundary: ordinary KAG bookmarks can be BMP/PNG/JPEG
// containers with appended/enclosed script data and custom file names. Observe
// saveBookMarkToFile itself instead of guessing progress from those names. The
// original call stays in its existing engine-tick JSPI stack, including cold I/O.
std::uint32_t game_save_sequence = 0;
std::size_t game_save_depth = 0;
bool game_save_flushing = false;
bool game_save_failed = false;

struct GameSaveFlushScope final {
    GameSaveFlushScope() { game_save_flushing = true; }
    ~GameSaveFlushScope() { game_save_flushing = false; }
    GameSaveFlushScope(const GameSaveFlushScope &) = delete;
    GameSaveFlushScope &operator=(const GameSaveFlushScope &) = delete;
};

class GameSaveCall final {
    bool tracked_;
    bool outermost_;
    bool completed_ = false;
    int id_ = 0;

public:
    GameSaveCall(bool tracked, bool history)
        : tracked_(tracked), outermost_(tracked && game_save_depth++ == 0) {
        if(outermost_) {
            game_save_failed = false;
            // A bounded positive integer is exact in JavaScript. A new id may
            // only wrap after billions of completed, non-overlapping saves.
            game_save_sequence = (game_save_sequence % 0x7fffffffU) + 1;
            id_ = static_cast<int>(game_save_sequence);
            krkr2_host_notify_game_save(0, id_, 0, history ? 1 : 0);
        }
    }

    [[nodiscard]] bool outermost() const { return outermost_; }
    [[nodiscard]] int id() const { return id_; }

    void complete(tjs_error status, const tTJSVariant &result) noexcept {
        if(!tracked_) return;
        // Extracted KAG/KAGEX methods return integer true/false. A wrapper that
        // discards that return is only a non-rejection witness: the Host must
        // also observe nonempty native file writes within this call's id.
        const bool success = status == TJS_S_OK &&
            (result.Type() == tvtVoid ||
             (result.Type() == tvtInteger && result.operator bool()));
        completed_ = true;
        game_save_failed = game_save_failed || !success;
        if(outermost_)
            krkr2_host_notify_game_save(1, id_, game_save_failed ? 0 : 1, 0);
    }

    ~GameSaveCall() {
        if(tracked_ && !completed_) {
            game_save_failed = true;
            if(outermost_) krkr2_host_notify_game_save(1, id_, 0, 0);
        }
        if(tracked_) --game_save_depth;
    }
};

// KAG3 thumbnails come from lockSnapshot(): it pile-copies fore.base, or keeps
// the image a system menu locked before opening, so dialogs and save screens
// on the primary layer are excluded. Host failures never affect the save call.
class KagSnapshot final {
    OwnedDispatch kag_;
    bool locked_ = false;

    bool call(const tjs_char *name) noexcept {
        try {
            tTJSVariant method;
            if(kag_->PropGet(0, name, nullptr, &method, kag_.get()) != TJS_S_OK ||
               method.Type() != tvtObject || !method.AsObjectNoAddRef()) return false;
            tTJSVariant ignored;
            return TJS_SUCCEEDED(method.AsObjectClosureNoAddRef().FuncCall(
                0, nullptr, nullptr, &ignored, 0, nullptr, kag_.get()));
        } catch(...) {
            return false;
        }
    }

public:
    explicit KagSnapshot(iTJSDispatch2 *kag) : kag_(retainDispatch(kag)) {
        locked_ = call(TJS_W("lockSnapshot"));
    }

    void publish(int id) noexcept {
        if(!locked_) return;
        try {
            tTJSVariant value;
            if(kag_->PropGet(0, TJS_W("snapshotLayer"), nullptr, &value, kag_.get()) != TJS_S_OK ||
               value.Type() != tvtObject || !value.AsObjectNoAddRef()) return;
            tTJSNI_BaseLayer *layer = nullptr;
            if(TJS_FAILED(value.AsObjectNoAddRef()->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) return;
            tTVPBaseTexture *image = layer->GetMainImage();
            if(!image || !image->Is32BPP()) return;
            const tjs_uint width = image->GetWidth(), height = image->GetHeight();
            if(!width || !height || width > 8192 || height > 8192) return;
            std::vector<unsigned char> rgba(static_cast<std::size_t>(width) * height * 4);
            for(tjs_uint y = 0; y < height; ++y) {
                const auto *row = static_cast<const unsigned char *>(image->GetScanLine(y));
                unsigned char *out = rgba.data() + static_cast<std::size_t>(y) * width * 4;
                for(tjs_uint x = 0; x < width; ++x, row += 4, out += 4) {
                    // Web layer memory is already R,G,B,A; snapshots are opaque.
                    out[0] = row[0]; out[1] = row[1]; out[2] = row[2]; out[3] = 255;
                }
            }
            krkr2_host_publish_game_save_image(id, rgba.data(), static_cast<int>(width),
                                               static_cast<int>(height));
        } catch(...) {
        }
    }

    ~KagSnapshot() {
        if(locked_) call(TJS_W("unlockSnapshot"));
    }

    KagSnapshot(const KagSnapshot &) = delete;
    KagSnapshot &operator=(const KagSnapshot &) = delete;
};

// KAG3/KAGEX save player bookmarks as saveBookMarkToFile(fn[, savehist=true]).
// System bookmarks such as the start anchor pass savehist=false: they are still
// native files, but they do not mark a player save point.
bool recordsHistory(tjs_int count, tTJSVariant **params) {
    return count < 2 || !params[1] || params[1]->Type() == tvtVoid ||
           params[1]->operator bool();
}

class GameSaveObserver final : public tTJSDispatch {
    tTJSVariant method_;
    iTJSDispatch2 *kag_; // Owned by GameSaveWatch, which outlives active calls.
    bool active_ = true;

public:
    GameSaveObserver(const tTJSVariant &method, iTJSDispatch2 *kag)
        : method_(method), kag_(kag) {}

    void deactivate(bool abandon_receiver = false) {
        active_ = false;
        // Only a failed detach during final teardown uses this path. The
        // runtime is stopped, so cached aliases cannot extend a receiver cycle
        // after Host ownership is released. In-flight calls own a local copy.
        if(abandon_receiver)
            method_ = tTJSVariant(method_.AsObjectNoAddRef(), nullptr);
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
        // Preserve the complete TJS closure (including an explicit incontextof
        // receiver), and keep both function and receiver alive across JSPI.
        const tTJSVariant method(method_);
        tTJSVariant ignored_result;
        if(!result) result = &ignored_result;
        const bool history = recordsHistory(count, params);
        GameSaveCall observed(active_, history);
        std::unique_ptr<KagSnapshot> snapshot;
        if(observed.outermost() && history) snapshot = std::make_unique<KagSnapshot>(kag_);
        const auto status = method.AsObjectClosureNoAddRef().FuncCall(
            flag, name, hint, result, count, params, objthis);
        if(snapshot && status == TJS_S_OK) snapshot->publish(observed.id());
        snapshot.reset();
        observed.complete(status, *result);
        return status; // The scope pairs a thrown exception with end(false).
    }
};

class GameSaveWatch final {
    OwnedDispatch kag_;
    tTJSVariant original_;
    OwnedDispatch observer_;
    bool detached_ = false;

public:
    GameSaveWatch(iTJSDispatch2 *kag, const tTJSVariant &method)
        : kag_(retainDispatch(kag)), original_(method),
          observer_(new GameSaveObserver(method, kag)) {}

    bool matches(iTJSDispatch2 *kag, const tTJSVariant &method) const {
        return kag_.get() == kag && method.Type() == tvtObject &&
               method.AsObjectNoAddRef() == observer_.get();
    }

    bool install() {
        const tTJSVariant callback(observer_.get(), nullptr);
        return kag_->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
            TJS_W("saveBookMarkToFile"), nullptr, &callback, kag_.get()) == TJS_S_OK;
    }

    bool detach(bool closing = false) noexcept {
        if(detached_) return true;
        try {
            if(closing && kag_->IsValid(0, nullptr, nullptr, kag_.get()) == TJS_S_FALSE) {
                detached_ = true;
                static_cast<GameSaveObserver *>(observer_.get())->deactivate();
                return true;
            }
            tTJSVariant current;
            const auto status = kag_->PropGet(TJS_IGNOREPROP,
                TJS_W("saveBookMarkToFile"), nullptr, &current, kag_.get());
            if(status == TJS_E_MEMBERNOTFOUND ||
               (status == TJS_S_OK && !matches(kag_.get(), current))) {
                detached_ = true; // Preserve deletion/replacement by the game.
            } else if(status == TJS_S_OK) {
                detached_ = kag_->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                    TJS_W("saveBookMarkToFile"), nullptr, &original_, kag_.get()) == TJS_S_OK;
            }
            if(detached_) static_cast<GameSaveObserver *>(observer_.get())->deactivate();
            return detached_;
        } catch(...) {
            return false;
        }
    }

    ~GameSaveWatch() {
        if(!detach()) {
            static_cast<GameSaveObserver *>(observer_.get())->deactivate(true);
            std::fprintf(stderr, "[game-save] cannot detach native save observer\n");
        }
    }
};
std::unique_ptr<GameSaveWatch> game_save_watch;

int observeGameSaves() noexcept {
    // No script FuncCall/getter/setter is evaluated here. Only inspect and
    // replace an ordinary function member between fully drained engine ticks.
    if(native_exit_requested || !TVPStartupSuccess || game_save_depth != 0 || game_save_flushing ||
       !krkr2_host_main_loop_is_idle()) return 0;
    try {
        ScriptDispatch script;
        tTJSVariant kag_value, method;
        iTJSDispatch2 *kag = nullptr;
        if(script.value && script.value->PropGet(TJS_IGNOREPROP,
            TJS_W("kag"), nullptr, &kag_value, script.value) == TJS_S_OK &&
           kag_value.Type() == tvtObject) kag = kag_value.AsObjectNoAddRef();
        if(kag) {
            const auto status = kag->PropGet(TJS_IGNOREPROP,
                TJS_W("saveBookMarkToFile"), nullptr, &method, kag);
            if(status != TJS_S_OK && status != TJS_E_MEMBERNOTFOUND)
                return kMethodUnavailable;
        }
        if(game_save_watch && game_save_watch->matches(kag, method)) return 1;
        if(game_save_watch && !game_save_watch->detach()) return kMethodUnavailable;
        game_save_watch.reset();
        if(!kag || method.Type() != tvtObject || !method.AsObjectNoAddRef() ||
           method.AsObjectNoAddRef()->IsInstanceOf(0, nullptr, nullptr,
               TJS_W("Function"), method.AsObjectNoAddRef()) != TJS_S_TRUE)
            return script.value ? 1 : 0; // Built-in structured-file observer.
        auto watch = std::make_unique<GameSaveWatch>(kag, method);
        if(!watch->install()) return kMethodUnavailable;
        game_save_watch = std::move(watch);
        return 1;
    } catch(...) {
        return kScriptException;
    }
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

void TVPWebRequestNativeExit(int status) {
    if(native_exit_completed) return;
    if(native_exit_requested) {
        // An actual failure during pending normal shutdown must still reach
        // the Host; duplicate normal requests do not restart finalization.
        if(native_exit_status == 0 && status != 0) {
            native_exit_status = status;
            krkr2_host_notify_native_exit(status);
        }
        return;
    }
    native_exit_requested = true;
    native_exit_status = status;
    krkr2_host_notify_native_exit(status);
}

// Native serializer writes use the same transaction depth as observed KAG calls.
// The caller invokes this only after validating an image-backed file prefix.
// Outside a KAG bookmark call, such a file is the game's own player save slot.
void *TVPBeginGameSaveObservation() noexcept {
    if(!TVPStartupSuccess || native_exit_requested || game_save_flushing) return nullptr;
    try { return new GameSaveCall(true, true); }
    catch(...) { return nullptr; }
}

void TVPEndGameSaveObservation(void *call, bool success) noexcept {
    if(!call) return;
    auto *observed = static_cast<GameSaveCall *>(call);
    const tTJSVariant result;
    observed->complete(success ? TJS_S_OK : TJS_E_FAIL, result);
    delete observed;
}

// 0: startup/tick not ready; 1: native serializers and optional KAG observed;
// -3: member read/write/detach failed; -5: TJS lookup/installation exception.
extern "C" EMSCRIPTEN_KEEPALIVE int krkr2_host_observe_game_saves() {
    return observeGameSaves();
}

// Explicit Web GAME_SAVE export boundary. A native save menu can update its
// bookkeeping after saveBookMarkToFile has already persisted the slot/index.
// Flush the game's registered system-data callback between drained ticks. This
// creates no bookmark and supplies no progress witness. The JSPI export retains
// these closures if the native callback reads a cold resource while flushing.
// 0: no callback; 1: flushed; negative: failed (never silently export old data).
static int flushNativeSystemData(bool closing) {
    if(!TVPStartupSuccess) return closing ? 0 : kBookmarkRejected;
    if(game_save_depth != 0 || game_save_flushing ||
       !krkr2_host_main_loop_is_idle()) return kBookmarkRejected;
    try {
        const GameSaveFlushScope flushing;
        ScriptDispatch script;
        if(!script.value) return closing ? 0 : kScriptUnavailable;
        tTJSVariant kag_value, method, result;
        if(script.value->PropGet(TJS_IGNOREPROP, TJS_W("kag"), nullptr,
                                 &kag_value, script.value) != TJS_S_OK ||
           kag_value.Type() != tvtObject || !kag_value.AsObjectNoAddRef())
            return 0;
        auto *kag = kag_value.AsObjectNoAddRef();
        // Games may invalidate their own KAG while accepting window close.
        // Only confirmed shutdown can skip that already unavailable receiver.
        if(closing && kag->IsValid(0, nullptr, nullptr, kag) == TJS_S_FALSE) return 0;
        const auto status = kag->PropGet(TJS_IGNOREPROP,
            TJS_W("saveSystemVariables"), nullptr, &method, kag);
        if(status == TJS_E_MEMBERNOTFOUND) return 0;
        if(status != TJS_S_OK || method.Type() != tvtObject ||
           !method.AsObjectNoAddRef() ||
           method.AsObjectNoAddRef()->IsInstanceOf(0, nullptr, nullptr,
               TJS_W("Function"), method.AsObjectNoAddRef()) != TJS_S_TRUE)
            return kMethodUnavailable;
        if(method.AsObjectClosureNoAddRef().FuncCall(0, nullptr, nullptr,
            &result, 0, nullptr, kag) != TJS_S_OK) return kMethodUnavailable;
        return result.Type() == tvtVoid ||
            (result.Type() == tvtInteger && result.operator bool())
            ? 1 : kBookmarkRejected;
    } catch(...) {
        return kScriptException;
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE int krkr2_host_flush_game_saves() {
    if(native_exit_requested) return kBookmarkRejected;
    return flushNativeSystemData(false);
}

// JSPI export: invoked only after the request callback returns and the active
// frame drains. Keep VLFS and TJS alive through the potentially suspended flush;
// detach while the receiver is still valid, then run ordinary native teardown.
extern "C" EMSCRIPTEN_KEEPALIVE int krkr2_host_finalize_native_exit() {
    if(native_exit_completed) return kBookmarkSucceeded;
    if(!native_exit_requested || native_exit_status != 0 || game_save_depth != 0 ||
       game_save_flushing || !krkr2_host_main_loop_is_idle()) return kBookmarkRejected;
    try {
        const int flushed = flushNativeSystemData(true);
        if(flushed < 0) return flushed;
        if(game_save_watch && !game_save_watch->detach(true)) return kMethodUnavailable;
        game_save_watch.reset();
        bootstrap_load.reset();
        pending_load.reset();
        TVPSystemUninit();
        native_exit_completed = true;
        return kBookmarkSucceeded;
    } catch(...) {
        return kScriptException;
    }
}

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
    game_save_watch.reset();
}
