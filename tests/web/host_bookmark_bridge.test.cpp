// Host-boundary test: fake only the TJS/Cocos dependencies, compile the actual
// Web bridge below. No reconstructed engine implementation or game is mocked.
#include <cassert>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

using tjs_char = wchar_t;
using tjs_int32 = int;
#define TJS_W(value) L##value
#define EMSCRIPTEN_KEEPALIVE
constexpr int TJS_S_OK = 0;
constexpr int TJS_S_TRUE = 1;
constexpr int TJS_S_FALSE = 2;
enum { tvtVoid, tvtObject, tvtString, tvtInteger };
struct iTJSDispatch2;
struct TestString {
    std::wstring value;
    int GetLength() const { return value.size(); }
};
struct tTJSVariant {
    int type = tvtVoid;
    iTJSDispatch2 *object = nullptr;
    TestString string;
    int integer = 0;
    tTJSVariant() = default;
    explicit tTJSVariant(int value) : type(tvtInteger), integer(value) {}
    explicit tTJSVariant(iTJSDispatch2 *value) : type(tvtObject), object(value) {}
    explicit tTJSVariant(const wchar_t *value) : type(tvtString), string{value} {}
    int Type() const { return type; }
    iTJSDispatch2 *AsObjectNoAddRef() const { return object; }
    const TestString *AsStringNoAddRef() const { return &string; }
    explicit operator bool() const { return type == tvtObject ? object != nullptr : integer != 0; }
};
struct iTJSDispatch2 {
    std::map<std::wstring, tTJSVariant> properties;
    bool function = false;
    bool throws = false;
    int callException = 0;
    int calls = 0;
    int slot = -1;
    int PropGet(int, const wchar_t *name, void *, tTJSVariant *result,
                iTJSDispatch2 *) {
        if(throws) throw std::runtime_error("script unavailable");
        auto found = properties.find(name);
        if(found == properties.end()) return -1;
        *result = found->second;
        return TJS_S_OK;
    }
    int IsInstanceOf(int, void *, void *, const wchar_t *, iTJSDispatch2 *) {
        return function ? TJS_S_TRUE : TJS_S_FALSE;
    }
    int FuncCall(int, void *, void *, tTJSVariant *result, int count,
                 tTJSVariant **arguments, iTJSDispatch2 *) {
        if(callException == 1) throw std::runtime_error("bookmark test failure");
        if(callException == 2) throw 7;
        assert(count == 1);
        ++calls;
        slot = arguments[0]->integer;
        *result = tTJSVariant(1);
        return TJS_S_OK;
    }
};
iTJSDispatch2 *testGlobal = nullptr;
iTJSDispatch2 *TVPGetScriptDispatch() { return testGlobal; }
namespace cocos2d {
struct Scheduler {
    std::function<void()> queued;
    void performFunctionInCocosThread(std::function<void()> callback) {
        queued = std::move(callback);
    }
};
struct Director {
    Scheduler scheduler;
    static Director *getInstance() { static Director instance; return &instance; }
    Scheduler *getScheduler() { return &scheduler; }
};
}

bool TVPStartupSuccess = false;
bool testLoopRegistered = false;
bool testLoopPending = false;
bool testLoopStopping = false;
#define EM_JS(type, name, args, ...) type name args { \
    return testLoopRegistered && !testLoopPending && !testLoopStopping; \
}
#include "cpp/core/environ/web/HostBookmarkBridge.cpp"

int main() {
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    testLoopRegistered = true;
    iTJSDispatch2 global, kag, load, save;
    testGlobal = &global;
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    global.properties[L"kag"] = tTJSVariant(&kag);
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    kag.properties[L"loadBookMark"] = tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr));
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    kag.properties[L"loadBookMark"] = tTJSVariant(&load);
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    load.function = true;
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    TVPStartupSuccess = true;
    kag.properties[L"saveBookMark"] = tTJSVariant(&save);
    save.function = true;
    kag.properties[L"currentLabel"] = tTJSVariant(L"");
    kag.properties[L"inStable"] = tTJSVariant(1);
    // A callable loader and default stable flag can be visible while a JSPI
    // tick is still suspended within script initialization.
    testLoopPending = true;
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    assert(krkr2_host_load_bookmark(1999) == kBookmarkRejected);
    assert(krkr2_host_load_bookmark_state() == kLoadIdle);
    assert(krkr2_host_bookmark_is_ready() == 0);
    testLoopPending = false;
    // A complete startup tick can leave KAG awaiting input without notifying
    // stable or starting its first scenario process. Existing saves can load.
    kag.properties[L"inStable"] = tTJSVariant(0);
    kag.properties[L"isFirstProcess"] = tTJSVariant(1);
    assert(krkr2_host_load_bookmark_is_ready() == 1);
    assert(krkr2_host_bookmark_is_ready() == 0);
    assert(krkr2_host_load_bookmark(1999) == 0);
    assert(krkr2_host_load_bookmark_state() == 1);
    assert(load.calls == 0);
    // The callback executes within its own Cocos tick: do not reject it merely
    // because that tick is active or the startup scene is not a savepoint.
    testLoopPending = true;
    cocos2d::Director::getInstance()->scheduler.queued();
    assert(load.calls == 1 && load.slot == 1999);
    assert(krkr2_host_load_bookmark_state() == 2);
    testLoopPending = false;
    kag.properties[L"inStable"] = tTJSVariant(1);
    assert(krkr2_host_bookmark_is_ready() == 0);
    kag.properties[L"currentLabel"] = tTJSVariant(L"*chapter");
    assert(krkr2_host_bookmark_is_ready() == 1);
    kag.properties.erase(L"inStable");
    assert(krkr2_host_bookmark_is_ready() == 0);
    kag.properties[L"inStable"] = tTJSVariant(&kag);
    assert(krkr2_host_bookmark_is_ready() == 0);
    kag.properties[L"inStable"] = tTJSVariant(0);
    assert(krkr2_host_bookmark_is_ready() == 0);
    assert(krkr2_host_save_bookmark(1999) == kBookmarkRejected);
    kag.properties[L"inStable"] = tTJSVariant(1);
    testLoopStopping = true;
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    assert(krkr2_host_bookmark_is_ready() == 0);
    assert(krkr2_host_load_bookmark(1999) == kBookmarkRejected);
    assert(krkr2_host_save_bookmark(1999) == kBookmarkRejected);
    testLoopStopping = false;
    save.callException = 1;
    assert(krkr2_host_save_bookmark(1999) == kScriptException);
    save.callException = 2;
    assert(krkr2_host_save_bookmark(1999) == kScriptException);
    global.throws = true;
    assert(krkr2_host_load_bookmark_is_ready() == 0);
}
