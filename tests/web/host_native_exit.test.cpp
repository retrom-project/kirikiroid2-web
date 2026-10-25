// The browser-only coordinator operates on the actual KAG observer and flush.
#include "host_bookmark_bridge_fixture.h"
#include "cpp/core/environ/web/HostBookmarkBridge.cpp"

int main(int argc, char **argv) {
    const std::string mode = argc > 1 ? argv[1] : "normal";
    iTJSDispatch2 global, kag, native, flush;
    native.function = flush.function = true;
    testGlobal = &global;
    global.properties[L"kag"] = tTJSVariant(&kag);
    kag.properties[L"saveBookMarkToFile"] = tTJSVariant(&native);
    kag.properties[L"saveSystemVariables"] = tTJSVariant(&flush);
    testLoopRegistered = TVPStartupSuccess = true;
    assert(krkr2_host_observe_game_saves() == 1);
    assert(liveDispatches == 1);
    int reads = 0;
    kag.onGet = [&](const wchar_t *) { ++reads; };
    if(mode == "invalidated") {
        kag.properties.clear(); // Native invalidation releases its members.
        kag.valid = false;
        // The same unavailable receiver is a real error during ordinary play.
        assert(krkr2_host_observe_game_saves() < 0);
    }
    const int before = reads;
    TVPWebRequestNativeExit(0);
    TVPWebRequestNativeExit(0);
    assert((testNativeExitStatuses == std::vector<int>{0}));
    assert(testSystemShutdowns == 0 && flush.calls == 0);
    assert(krkr2_host_observe_game_saves() == 0 && reads == before);
    testLoopPending = true;
    assert(krkr2_host_finalize_native_exit() < 0);
    assert(testSystemShutdowns == 0 && flush.calls == 0);
    testLoopPending = false;
    if(mode == "nonzero") {
        TVPWebRequestNativeExit(9);
        assert((testNativeExitStatuses == std::vector<int>{0, 9}));
        assert(krkr2_host_finalize_native_exit() < 0 && testSystemShutdowns == 0);
        krkr2_host_cancel_bookmark_load();
        return 0;
    }
    if(mode == "failed-flush") {
        flush.callStatus = TJS_E_FAIL;
        assert(krkr2_host_finalize_native_exit() < 0);
        assert(testSystemShutdowns == 0);
        krkr2_host_cancel_bookmark_load();
        return 0;
    }
    testSystemShutdown = [&]() {
        assert(liveDispatches == 0); // Detach before invalidating the receiver.
        if(kag.valid) {
            assert(flush.calls == 1);
            assert(kag.properties[L"saveBookMarkToFile"].AsObjectNoAddRef() == &native);
        } else {
            assert(flush.calls == 0); // Never call an invalidated game object.
        }
    };
    assert(krkr2_host_finalize_native_exit() == 0);
    assert(testSystemShutdowns == 1);
    assert(krkr2_host_finalize_native_exit() == 0 && testSystemShutdowns == 1);
    assert(krkr2_host_observe_game_saves() == 0);
    assert((testNativeExitStatuses == std::vector<int>{0}));
    krkr2_host_cancel_bookmark_load();
    testSystemShutdown = nullptr;
}
