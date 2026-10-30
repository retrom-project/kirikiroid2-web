// Player bookmarks publish KAG's own snapshot; system bookmarks do not.
#include "host_bookmark_bridge_fixture.h"
#include "cpp/core/environ/web/HostBookmarkBridge.cpp"

struct NativeSave final : iTJSDispatch2 {
    std::function<void()> during;
    NativeSave() { function = true; }
    tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *result,
                      tjs_int, tTJSVariant **, iTJSDispatch2 *) override {
        ++calls;
        if(during) during();
        *result = tTJSVariant(1);
        return TJS_S_OK;
    }
};

int main() {
    iTJSDispatch2 global, kag, lock, unlock, snapshot;
    NativeSave native;
    tTJSNI_BaseLayer layer;
    layer.image.width = 2;
    layer.image.height = 1;
    layer.image.pixels = {10, 20, 30, 0, 40, 50, 60, 7};
    snapshot.nativeLayer = &layer;
    lock.function = unlock.function = true;
    int locked = 0;
    lock.onCall = [&](tTJSVariant *, int, tTJSVariant **) { ++locked; kag.properties[L"snapshotLayer"] = tTJSVariant(&snapshot); };
    unlock.onCall = [&](tTJSVariant *, int, tTJSVariant **) { --locked; };
    testGlobal = &global;
    global.properties[L"kag"] = tTJSVariant(&kag);
    kag.properties[L"saveBookMarkToFile"] = tTJSVariant(&native);
    kag.properties[L"lockSnapshot"] = tTJSVariant(&lock);
    kag.properties[L"unlockSnapshot"] = tTJSVariant(&unlock);
    testLoopRegistered = TVPStartupSuccess = true;
    assert(krkr2_host_observe_game_saves() == 1);
    auto save = [&](tTJSVariant *history) {
        const auto method = kag.properties.at(L"saveBookMarkToFile");
        tTJSVariant path(L"/savedata/data1.bmp"), result;
        tTJSVariant *params[]{&path, history};
        return method.AsObjectClosureNoAddRef().FuncCall(0, nullptr, nullptr, &result, history ? 2 : 1, params, &kag);
    };

    // The snapshot is locked before the original call, so it reuses an image a
    // system menu locked earlier, and is released before the end witness.
    native.during = [&] { assert(locked == 1); };
    assert(save(nullptr) == TJS_S_OK);
    assert(locked == 0 && lock.calls == 1 && unlock.calls == 1);
    assert(testGameSaveEvents.size() == 2 && testGameSaveEvents[0].history && testGameSaveEvents[1].success);
    assert(testGameSaveImages.size() == 1 && testGameSaveImages[0].id == testGameSaveEvents[0].id);
    assert(testGameSaveImages[0].width == 2 && testGameSaveImages[0].height == 1);
    assert((testGameSaveImages[0].pixels == std::vector<unsigned char>{10, 20, 30, 255, 40, 50, 60, 255}));

    // savehist=false is a system bookmark such as the KAG start anchor.
    native.during = [&] { assert(locked == 0); };
    tTJSVariant system(0);
    assert(save(&system) == TJS_S_OK);
    assert(testGameSaveEvents.size() == 4 && !testGameSaveEvents[2].history);
    assert(testGameSaveImages.size() == 1 && lock.calls == 1);

    // A failing snapshot must not change the game's own save result.
    lock.callException = 1;
    tTJSVariant player(1);
    assert(save(&player) == TJS_S_OK && native.calls == 3);
    assert(testGameSaveEvents.size() == 6 && testGameSaveEvents[4].history && testGameSaveEvents[5].success);
    assert(testGameSaveImages.size() == 1 && unlock.calls == 1);
    lock.callException = 0;

    // Release the watch while the stack-owned KAG object is still alive.
    krkr2_host_cancel_bookmark_load();
    assert(kag.properties.at(L"saveBookMarkToFile").AsObjectNoAddRef() == &native);
    kag.properties.clear();
    global.properties.clear();
}
