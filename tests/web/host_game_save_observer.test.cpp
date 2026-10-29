// Exercise the production pass-through observer at its TJS call boundary.
#include "host_bookmark_bridge_fixture.h"
#include "cpp/core/environ/web/HostBookmarkBridge.cpp"

// Keep the old bridge linkable so the red test reaches an actual successful
// native save, rather than stopping at a missing-export compilation error.
extern "C" int krkr2_host_observe_game_saves() __attribute__((weak));
void *TVPBeginGameSaveObservation() noexcept __attribute__((weak));
void TVPEndGameSaveObservation(void *, bool) noexcept __attribute__((weak));

struct NativeSave final : iTJSDispatch2 {
    std::function<tjs_error(tjs_uint32, const tjs_char *, tjs_uint32 *,
                           tTJSVariant *, tjs_int, tTJSVariant **,
                           iTJSDispatch2 *)> call;
    NativeSave() { function = true; }
    tjs_error FuncCall(tjs_uint32 flags, const tjs_char *name, tjs_uint32 *hint,
                      tTJSVariant *result, tjs_int count, tTJSVariant **params,
                      iTJSDispatch2 *receiver) override {
        ++calls;
        lastReceiver = receiver;
        return call(flags, name, hint, result, count, params, receiver);
    }
};

int observe() {
    auto *volatile observer = krkr2_host_observe_game_saves;
    return observer ? observer() : 0;
}

int main() {
    iTJSDispatch2 global, kag, otherKag, boundThis;
    NativeSave native, replacement;
    testGlobal = &global;
    global.properties[L"kag"] = tTJSVariant(&kag);
    kag.properties[L"saveBookMarkToFile"] = tTJSVariant(&native);
    testLoopRegistered = TVPStartupSuccess = true;
    auto current = [&]() {return kag.properties.at(L"saveBookMarkToFile");};
    auto call = [&](tTJSVariant *result = nullptr) {
        const auto method = current();
        tTJSVariant path(L"/custom saves/slot one.png"), history(1);
        tTJSVariant *params[]{&path, &history};
        return method.AsObjectClosureNoAddRef().FuncCall(
            17, L"member", nullptr, result, 2, params, &kag);
    };
    native.call = [&](tjs_uint32, const tjs_char *, tjs_uint32 *,
                      tTJSVariant *result, tjs_int count, tTJSVariant **params,
                      iTJSDispatch2 *receiver) {
        assert(count == 2 && params[0]->string.value == L"/custom saves/slot one.png");
        assert(receiver == &kag);
        *result = tTJSVariant(1);
        return TJS_S_OK;
    };

    observe();
    tTJSVariant result;
    assert(call(&result) == TJS_S_OK && result.integer == 1 && native.calls == 1);
    if(testGameSaveEvents.size() != 2)
        throw std::runtime_error("a real successful KAG save produced no begin/end witness");
    assert(testGameSaveEvents[0].phase == 0 && testGameSaveEvents[1].phase == 1);
    assert(testGameSaveEvents[0].id > 0 && testGameSaveEvents[0].id == testGameSaveEvents[1].id);
    assert(testGameSaveEvents[1].success);

    // Repeated polling installs once, never saves or loads, and bypasses script
    // property getters/setters while the main-loop stack is idle.
    auto wrapped = current();
    for(int i = 0; i != 50; ++i) assert(observe() == 1);
    assert(current().AsObjectNoAddRef() == wrapped.AsObjectNoAddRef());
    assert(native.calls == 1 && liveDispatches == 1);
    assert((global.lastGetFlags & TJS_IGNOREPROP) != 0);
    assert((kag.lastGetFlags & TJS_IGNOREPROP) != 0);
    assert((kag.lastSetFlags & TJS_IGNOREPROP) != 0);
    testLoopPending = true;
    assert(observe() == 0);
    testLoopPending = false;
    testLoopStopping = true;
    assert(observe() == 0);
    testLoopStopping = false;

    // Preserve every parameter, the dispatch flags/name/hint, return value,
    // status and receiver. Writes happen inside the original call, between
    // begin/end, and the observer does not request any write of its own.
    std::vector<std::string> order;
    testGameSaveCallback = [&](const TestGameSaveEvent &event) {
        order.push_back(event.phase == 0 ? "begin" : "end");
    };
    tjs_uint32 hint = 19;
    tTJSVariant path(L"arbitrary prefix.jpg"), history(0), extra(L"extra");
    tTJSVariant *params[]{&path, &history, &extra};
    native.call = [&](tjs_uint32 flags, const tjs_char *name, tjs_uint32 *actualHint,
                      tTJSVariant *actualResult, tjs_int count, tTJSVariant **actualParams,
                      iTJSDispatch2 *receiver) {
        assert(flags == 23 && ttstr(name) == L"save" && actualHint == &hint);
        assert(count == 3 && actualParams == params && actualResult == &result);
        assert(receiver == &kag);
        order.push_back("original");
        order.push_back("thumbnail close");
        order.push_back("complete data close");
        *actualResult = tTJSVariant(-1);
        *actualHint = 27;
        return TJS_S_OK;
    };
    assert(wrapped.AsObjectClosureNoAddRef().FuncCall(23, L"save", &hint,
        &result, 3, params, &kag) == TJS_S_OK);
    assert(result.integer == -1 && hint == 27);
    assert((order == std::vector<std::string>{"begin", "original", "thumbnail close", "complete data close", "end"}));
    assert(testGameSaveEvents.back().success);

    // KAG and the extracted KAGEX resource return integer true/false. A void
    // wrapper is only a non-rejection witness; the Host must require bytes.
    for(int outcome = 0; outcome != 5; ++outcome) {
        const auto before = testGameSaveEvents.size();
        native.call = [&](tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *out,
                          tjs_int, tTJSVariant **, iTJSDispatch2 *) {
            if(outcome == 0) *out = tTJSVariant(0);
            if(outcome == 1) *out = tTJSVariant();
            if(outcome == 2) *out = tTJSVariant(1);
            if(outcome == 3) *out = tTJSVariant(L"unknown success contract");
            if(outcome == 4) *out = tTJSVariant(&boundThis);
            return outcome == 2 ? TJS_E_FAIL : TJS_S_OK;
        };
        const auto status = call(&result);
        assert(status == (outcome == 2 ? TJS_E_FAIL : TJS_S_OK));
        assert(testGameSaveEvents.size() == before + 2);
        assert(testGameSaveEvents.back().success == (outcome == 1));
        if(outcome == 3) assert(result.Type() == tvtString);
        if(outcome == 4) assert(result.AsObjectNoAddRef() == &boundThis);
    }
    native.call = [&](tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *out,
                      tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        assert(out); // Capture an ignored return without changing its value.
        *out = tTJSVariant();
        return TJS_S_OK;
    };
    assert(call() == TJS_S_OK && testGameSaveEvents.back().success);

    // Exceptions are paired with failure, then rethrown unchanged. Neither a
    // throwing Host callback nor an unsuccessful native call retries the save.
    for(int kind : {1, 2}) {
        const auto before = native.calls;
        native.call = [&](tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *,
                          tjs_int, tTJSVariant **, iTJSDispatch2 *) -> tjs_error {
            if(kind == 1) throw std::runtime_error("native save failure");
            throw 314;
        };
        bool caught = false;
        try { call(); }
        catch(const std::runtime_error &error) { caught = kind == 1 && std::string(error.what()) == "native save failure"; }
        catch(int value) { caught = kind == 2 && value == 314; }
        assert(caught && native.calls == before + 1);
        assert(testGameSaveEvents.back().phase == 1 && !testGameSaveEvents.back().success);
    }
    testGameSaveCallback = [](const TestGameSaveEvent &) { throw std::runtime_error("Host callback failure"); };
    native.call = [](tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *out,
                     tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        *out = tTJSVariant(1);
        return TJS_S_OK;
    };
    assert(call(&result) == TJS_S_OK && result.integer == 1);
    testGameSaveCallback = {};

    // A recursive call remains inside the same original native transaction.
    // Also reject a reentrant observation query before the JSPI scheduler has
    // had an opportunity to publish its pending Promise.
    bool recursive = false;
    native.call = [&](tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *out,
                      tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        assert(observe() == 0);
        if(!recursive) {
            recursive = true;
            tTJSVariant inner;
            assert(call(&inner) == TJS_S_OK);
            recursive = false;
        }
        *out = tTJSVariant(1);
        return TJS_S_OK;
    };
    auto nestedBefore = testGameSaveEvents.size();
    auto nestedCalls = native.calls;
    assert(call(&result) == TJS_S_OK);
    assert(native.calls == nestedCalls + 2 && testGameSaveEvents.size() == nestedBefore + 2);
    native.call = [](tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *out,
                     tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        *out = tTJSVariant(1);
        return TJS_S_OK;
    };

    // A script replacement can retain and call the previous observed method.
    // Rechecking wraps the replacement once and leaves the cached method a
    // pass-through, with one pair for the outer native save transaction.
    const auto cached = current();
    replacement.call = [&](tjs_uint32 flags, const tjs_char *name, tjs_uint32 *h,
                           tTJSVariant *out, tjs_int count, tTJSVariant **args,
                           iTJSDispatch2 *receiver) {
        return cached.AsObjectClosureNoAddRef().FuncCall(flags, name, h, out, count, args, receiver);
    };
    kag.properties[L"saveBookMarkToFile"] = tTJSVariant(&replacement);
    assert(observe() == 1);
    auto beforeEvents = testGameSaveEvents.size();
    const auto beforeNative = native.calls;
    assert(call(&result) == TJS_S_OK);
    assert(native.calls == beforeNative + 1 && replacement.calls == 1);
    assert(testGameSaveEvents.size() == beforeEvents + 2);

    // Bound TJS closures retain their original receiver, including when the
    // caller supplies a different context. No raw-object-only invocation.
    krkr2_host_cancel_bookmark_load();
    kag.properties[L"saveBookMarkToFile"] = tTJSVariant(&native, &boundThis);
    native.call = [&](tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *out,
                      tjs_int, tTJSVariant **, iTJSDispatch2 *receiver) {
        assert(receiver == &boundThis);
        *out = tTJSVariant(1);
        return TJS_S_OK;
    };
    assert(observe() == 1);
    assert(call(&result) == TJS_S_OK);
    krkr2_host_cancel_bookmark_load();
    assert(current().AsObjectThisNoAddRef() == &boundThis);

    // A new Kag must detach the old hook, and stop must preserve replacements
    // installed by the game instead of overwriting them with stale methods.
    kag.properties[L"saveBookMarkToFile"] = tTJSVariant(&native);
    assert(observe() == 1);
    otherKag.properties[L"saveBookMarkToFile"] = tTJSVariant(&replacement);
    global.properties[L"kag"] = tTJSVariant(&otherKag);
    assert(observe() == 1);
    assert(current().AsObjectNoAddRef() == &native);
    otherKag.properties[L"saveBookMarkToFile"] = tTJSVariant(&native);
    krkr2_host_cancel_bookmark_load();
    assert(otherKag.properties[L"saveBookMarkToFile"].AsObjectNoAddRef() == &native);

    // Failed installation and failed detach must not retain a receiver cycle
    // or leave a live notification source after stop. A later poll may retry.
    global.properties[L"kag"] = tTJSVariant(&kag);
    kag.setStatus = TJS_E_FAIL;
    assert(observe() < 0);
    kag.setStatus = TJS_S_OK;
    assert(observe() == 1);
    kag.throws = true;
    assert(observe() < 0);
    krkr2_host_cancel_bookmark_load();
    kag.throws = false;
    native.call = [](tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *out,
                     tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        *out = tTJSVariant(1);
        return TJS_S_OK;
    };
    beforeEvents = testGameSaveEvents.size();
    assert(call(&result) == TJS_S_OK);
    assert(testGameSaveEvents.size() == beforeEvents);

    // A teardown that cannot read the original member releases a closure's
    // receiver binding too; a stale property must not pin the destroyed Kag.
    kag.properties[L"saveBookMarkToFile"] = tTJSVariant(&native, &kag);
    assert(observe() == 1);
    kag.throws = true;
    krkr2_host_cancel_bookmark_load();
    kag.throws = false;
    assert(kag.references == 2); // local object + global.kag, no observer cycle.
    kag.properties[L"saveBookMarkToFile"] = tTJSVariant(&native);
    assert(observe() == 1);
    // Install can be attempted before a method exists, without creating it.
    global.properties.erase(L"kag");
    assert(observe() == 1); // The built-in structured-file observer is ready.
    assert(current().AsObjectNoAddRef() == &native);
    global.properties[L"kag"] = tTJSVariant(&kag);
    kag.properties.erase(L"saveBookMarkToFile");
    assert(observe() == 1);
    krkr2_host_cancel_bookmark_load();
    assert(game_save_depth == 0);

    // Reuse the native transaction fixture for independent serializer writes.
    // No KAG receiver, hidden save request, or return-value substitution.
    global.properties.erase(L"kag");
    if(!TVPBeginGameSaveObservation || !TVPEndGameSaveObservation)
        throw std::runtime_error("native structured-save transaction boundary unavailable");
    beforeEvents = testGameSaveEvents.size();
    void *outer = TVPBeginGameSaveObservation();
    assert(outer && observe() == 0);
    void *inner = TVPBeginGameSaveObservation();
    TVPEndGameSaveObservation(inner, true);
    assert(testGameSaveEvents.size() == beforeEvents + 1);
    TVPEndGameSaveObservation(outer, true);
    assert(game_save_depth == 0 && testGameSaveEvents.size() == beforeEvents + 2);
    assert(testGameSaveEvents.back().success);
    outer = TVPBeginGameSaveObservation();
    TVPEndGameSaveObservation(outer, false);
    assert(!testGameSaveEvents.back().success && game_save_depth == 0);
    outer = TVPBeginGameSaveObservation();
    inner = TVPBeginGameSaveObservation();
    TVPEndGameSaveObservation(inner, false);
    TVPEndGameSaveObservation(outer, true);
    assert(!testGameSaveEvents.back().success && game_save_depth == 0);
    TVPStartupSuccess = false;
    assert(TVPBeginGameSaveObservation() == nullptr);
    TVPStartupSuccess = true;

    result = tTJSVariant();
    wrapped = tTJSVariant();
    global.properties.clear();
    kag.properties.clear();
    otherKag.properties.clear();
    // cached deliberately retains the retired observer until this scope ends.
    assert(kag.references == 1 && otherKag.references == 1 && boundThis.references == 1);
    assert(global.references == 1 && replacement.references == 1);
    assert(liveDispatches == 1 && native.references == 2);
}
