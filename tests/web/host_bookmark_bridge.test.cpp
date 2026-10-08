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
    explicit operator bool() const { return integer != 0; }
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

#include "cpp/core/environ/web/HostBookmarkBridge.cpp"

int main() {
    assert(krkr2_host_load_bookmark_is_ready() == 0);
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
    // A loader can exist while game initialization is still running.
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    assert(krkr2_host_bookmark_is_ready() == 0);
    kag.properties[L"saveBookMark"] = tTJSVariant(&save);
    save.function = true;
    kag.properties[L"currentLabel"] = tTJSVariant(L"");
    kag.properties[L"inStable"] = tTJSVariant(0);
    assert(krkr2_host_load_bookmark_is_ready() == 0);
    // Native click-wait is stable even before a scenario save label exists.
    kag.properties[L"inStable"] = tTJSVariant(1);
    assert(krkr2_host_load_bookmark_is_ready() == 1);
    assert(krkr2_host_bookmark_is_ready() == 0);
    // Load once through the real queued bridge, without advancing startup.
    assert(krkr2_host_load_bookmark(1999) == 0);
    assert(krkr2_host_load_bookmark_state() == 1);
    assert(load.calls == 0);
    cocos2d::Director::getInstance()->scheduler.queued();
    assert(load.calls == 1 && load.slot == 1999);
    assert(krkr2_host_load_bookmark_state() == 2);
    assert(krkr2_host_bookmark_is_ready() == 0);
    kag.properties[L"currentLabel"] = tTJSVariant(L"*chapter");
    kag.properties[L"inStable"] = tTJSVariant(0);
    assert(krkr2_host_bookmark_is_ready() == 0);
    kag.properties[L"inStable"] = tTJSVariant(1);
    assert(krkr2_host_bookmark_is_ready() == 1);
    // A queued host operation must not call into a game that resumed running
    // after the readiness query but before the Cocos scheduler executes it.
    load_state.store(kLoadIdle);
    assert(krkr2_host_load_bookmark(1999) == 0);
    kag.properties[L"inStable"] = tTJSVariant(0);
    cocos2d::Director::getInstance()->scheduler.queued();
    assert(krkr2_host_load_bookmark_state() == kBookmarkRejected);
    assert(load.calls == 1);
    kag.properties[L"inStable"] = tTJSVariant(1);
    save.callException = 1;
    assert(krkr2_host_save_bookmark(1999) == kScriptException);
    save.callException = 2;
    assert(krkr2_host_save_bookmark(1999) == kScriptException);
    global.throws = true;
    assert(krkr2_host_load_bookmark_is_ready() == 0);
}
