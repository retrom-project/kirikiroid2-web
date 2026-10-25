// Native slots can be written before the game's save-menu bookkeeping updates.
// Export must flush registered native metadata without requesting another slot.
#include "host_bookmark_bridge_fixture.h"
#include "cpp/core/environ/web/HostBookmarkBridge.cpp"

extern "C" int krkr2_host_flush_game_saves() __attribute__((weak));
int flush() {
    auto *volatile operation = krkr2_host_flush_game_saves;
    return operation ? operation() : 0;
}

int main() {
    iTJSDispatch2 global, kag, native, bound;
    native.function = true;
    testGlobal = &global;
    global.properties[L"kag"] = tTJSVariant(&kag);
    kag.properties[L"saveSystemVariables"] = tTJSVariant(&native, &bound);
    testLoopRegistered = TVPStartupSuccess = true;
    bool persisted = false;
    native.onCall = [&](tTJSVariant *result, int count, tTJSVariant **) {
        assert(count == 0);
        assert(krkr2_host_observe_game_saves() == 0);
        assert(flush() == -4); // Polling/reentry cannot inspect a suspended flush.
        persisted = true;
        *result = tTJSVariant(); // The native callback normally returns void.
    };
    if(flush() != 1 || !persisted)
        throw std::runtime_error("native slot metadata was not flushed before export");
    assert(native.calls == 1 && native.lastReceiver == &bound);
    assert(testGameSaveEvents.empty()); // Metadata is never progress evidence.
    assert((global.lastGetFlags & TJS_IGNOREPROP) != 0);
    assert((kag.lastGetFlags & TJS_IGNOREPROP) != 0);

    TVPStartupSuccess = false;
    assert(flush() == -4 && native.calls == 1);
    TVPStartupSuccess = true;
    testLoopPending = true;
    assert(flush() == -4 && native.calls == 1);
    testLoopPending = false;
    testLoopStopping = true;
    assert(flush() == -4 && native.calls == 1);
    testLoopStopping = false;

    kag.properties.erase(L"saveSystemVariables");
    assert(flush() == 0); // Frameworks without this optional callback keep files.
    kag.properties[L"saveSystemVariables"] = tTJSVariant(1);
    assert(flush() == -3);
    kag.properties[L"saveSystemVariables"] = tTJSVariant(&native);
    native.callStatus = TJS_E_FAIL;
    assert(flush() == -3);
    native.callStatus = TJS_S_OK;
    native.onCall = {};
    native.callResult = 0;
    assert(flush() == -4);
    native.callException = 1;
    assert(flush() == -5);
    native.callException = 2;
    assert(flush() == -5);
    native.callException = 0;
    global.throws = true;
    assert(flush() == -5);
    global.throws = false;
    native.callResult = 1;
    assert(flush() == 1); // A failed callback must release the flushing guard.

    kag.properties.clear();
    global.properties.clear();
    testGlobal = nullptr;
    assert(native.references == 1 && kag.references == 1 && bound.references == 1);
}
