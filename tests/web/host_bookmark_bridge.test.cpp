// Host-boundary test: fake only the TJS/event dependencies, compile the actual
// Web bridge below. No reconstructed engine implementation or game is mocked.
#include <cassert>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

using tjs_char = wchar_t;
using tjs_int32 = int;
using tjs_uint32 = unsigned;
using tjs_int = int;
using tjs_error = int;
using ttstr = std::wstring;
#define TJS_W(value) L##value
#define EMSCRIPTEN_KEEPALIVE
constexpr int TJS_S_OK = 0;
constexpr int TJS_S_TRUE = 1;
constexpr int TJS_S_FALSE = 2;
constexpr int TJS_E_FAIL = -100;
constexpr int TJS_MEMBERENSURE = 0x200;
enum { tvtVoid, tvtObject, tvtString, tvtInteger };
struct iTJSDispatch2;
void retain(iTJSDispatch2 *);
void release(iTJSDispatch2 *);
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
    explicit tTJSVariant(iTJSDispatch2 *value, iTJSDispatch2 * = nullptr)
        : type(tvtObject), object(value) { retain(object); }
    explicit tTJSVariant(const wchar_t *value) : type(tvtString), string{value} {}
    tTJSVariant(const tTJSVariant &value)
        : type(value.type), object(value.object), string(value.string), integer(value.integer) {
        retain(object);
    }
    ~tTJSVariant() { release(object); }
    tTJSVariant &operator=(const tTJSVariant &value) {
        retain(value.object);
        release(object);
        type = value.type; object = value.object; string = value.string; integer = value.integer;
        return *this;
    }
    int Type() const { return type; }
    iTJSDispatch2 *AsObjectNoAddRef() const { return object; }
    const TestString *AsStringNoAddRef() const { return &string; }
    explicit operator bool() const { return type == tvtObject ? object != nullptr : integer != 0; }
    explicit operator int() const { return integer; }
};
struct iTJSDispatch2 {
    int references = 1;
    virtual ~iTJSDispatch2() = default;
    void AddRef() { ++references; }
    void Release() { if(--references == 0) delete this; }
    std::map<std::wstring, tTJSVariant> properties;
    bool function = false;
    bool throws = false;
    int callException = 0;
    int calls = 0;
    int slot = -1;
    int setStatus = TJS_S_OK;
    bool setThrows = false;
    int callStatus = TJS_S_OK;
    int callResult = 1;
    std::function<void(tTJSVariant *, int, tTJSVariant **)> onCall;
    int PropGet(int, const wchar_t *name, void *, tTJSVariant *result,
                iTJSDispatch2 *) {
        if(throws) throw std::runtime_error("script unavailable");
        auto found = properties.find(name);
        if(found == properties.end()) return -1;
        *result = found->second;
        return TJS_S_OK;
    }
    int PropSet(int, const wchar_t *name, void *, const tTJSVariant *value, iTJSDispatch2 *) {
        if(setThrows) throw std::runtime_error("property write failed");
        if(setStatus != TJS_S_OK) return setStatus;
        properties[name] = *value;
        return TJS_S_OK;
    }
    virtual int IsInstanceOf(tjs_uint32, const tjs_char *, tjs_uint32 *, const wchar_t *, iTJSDispatch2 *) {
        return function ? TJS_S_TRUE : TJS_S_FALSE;
    }
    virtual int FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *result, int count,
                 tTJSVariant **arguments, iTJSDispatch2 *) {
        if(callException == 1) throw std::runtime_error("bookmark test failure");
        if(callException == 2) throw 7;
        assert(count == 1);
        ++calls;
        slot = arguments[0]->integer;
        *result = tTJSVariant(callResult);
        if(onCall) onCall(result, count, arguments);
        return callStatus;
    }
};
void retain(iTJSDispatch2 *value) { if(value) value->AddRef(); }
void release(iTJSDispatch2 *value) { if(value) value->Release(); }
iTJSDispatch2 *testGlobal = nullptr;
iTJSDispatch2 *TVPGetScriptDispatch() {
    if(testGlobal) testGlobal->AddRef();
    return testGlobal;
}
int liveDispatches = 0;
struct tTJSDispatch : iTJSDispatch2 {
    tTJSDispatch() { ++liveDispatches; }
    virtual ~tTJSDispatch() { --liveDispatches; }
    virtual tjs_error IsValid(tjs_uint32, const tjs_char *, tjs_uint32 *, iTJSDispatch2 *) { return TJS_S_TRUE; }
};
namespace cocos2d {
struct Scheduler {
    std::function<void()> queued;
    void performFunctionInCocosThread(std::function<void()> callback) { queued = std::move(callback); }
};
struct Director {
    Scheduler scheduler;
    static Director *getInstance() { static Director instance; return &instance; }
    Scheduler *getScheduler() { return &scheduler; }
};
}
void deliverQueued() {
    auto callback = std::move(cocos2d::Director::getInstance()->scheduler.queued);
    callback();
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
    // The callback executes within its own engine tick: do not reject it merely
    // because that tick is active or the startup scene is not a savepoint.
    testLoopPending = true;
    deliverQueued();
    assert(load.calls == 1 && load.slot == 1999);
    assert(krkr2_host_load_bookmark_state() == 2);
    testLoopPending = false;
    // Cancel must also win when the queue is dispatched afterward.
    load_state.store(kLoadIdle);
    assert(krkr2_host_load_bookmark(1999) == kBookmarkSucceeded);
    krkr2_host_cancel_bookmark_load();
    deliverQueued();
    assert(krkr2_host_load_bookmark_state() == kBookmarkRejected);
    assert(load.calls == 1);
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
    global.throws = false;
    save.callException = 0;

    iTJSDispatch2 entry, replacement;
    entry.function = replacement.function = true;
    global.properties[L"loadFunction"] = tTJSVariant(&entry);
    const int receiverReferences = kag.references;
    auto invoke = [&](const tTJSVariant &callback, int slot) {
        tTJSVariant argument(slot), result;
        tTJSVariant *params = &argument;
        return callback.AsObjectNoAddRef()->FuncCall(
            0, nullptr, nullptr, &result, 1, &params, &kag);
    };
    auto request = [&] {
        assert(!pending_load);
        load_state.store(kLoadIdle);
        assert(krkr2_host_load_bookmark(1999) == kBookmarkSucceeded);
        assert(krkr2_host_load_bookmark(1999) == kLoadAlreadyRequested);
        deliverQueued();
    };
    auto balanced = [&] {
        assert(!pending_load);
        assert(kag.references == receiverReferences);
        assert(liveDispatches == 0);
        assert(kag.properties.at(L"loadBookMark").AsObjectNoAddRef() == &load);
    };

    // Framework entry return values are not load results. A deferred entry
    // can return false/void; only the requested original bookmark completes it.
    entry.callResult = 0;
    request();
    assert(krkr2_host_load_bookmark_state() == kLoadPending);
    assert(kag.references > receiverReferences);
    {
        tTJSVariant observer = kag.properties.at(L"loadBookMark");
        assert(invoke(observer, 5) == TJS_S_OK);
        assert(krkr2_host_load_bookmark_state() == kLoadPending);
        assert(invoke(observer, 1999) == TJS_S_OK);
        assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
        // An alias retained by a script remains callable but cannot complete
        // the operation again after the one-shot observer is detached.
        load.callResult = 0;
        assert(invoke(observer, 1999) == TJS_S_OK);
        assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
        load.callResult = 1;
    }
    balanced();

    // Public KAGEX variants also call the original loader synchronously.
    entry.onCall = [&](tTJSVariant *result, int, tTJSVariant **) {
        const auto observer = kag.properties.at(L"loadBookMark");
        invoke(observer, 1999);
        *result = tTJSVariant();
    };
    request();
    assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
    balanced();
    entry.onCall = {};

    for(int failure : {0, 1, 2, 3}) {
        request();
        {
            auto observer = kag.properties.at(L"loadBookMark");
            load.callResult = failure == 0 ? 0 : 1;
            load.callStatus = failure == 1 ? TJS_E_FAIL : TJS_S_OK;
            load.callException = failure >= 2 ? failure - 1 : 0;
            try { invoke(observer, 1999); assert(failure < 2); }
            catch(...) { assert(failure >= 2); }
            assert(krkr2_host_load_bookmark_state() ==
                (failure == 0 ? kBookmarkRejected : failure == 1 ? kMethodUnavailable : kScriptException));
        }
        load.callResult = 1; load.callStatus = TJS_S_OK; load.callException = 0;
        balanced();
    }

    request();
    {
        auto observer = kag.properties.at(L"loadBookMark");
        kag.properties[L"loadBookMark"] = tTJSVariant(&replacement);
        invoke(observer, 1999);
        assert(krkr2_host_load_bookmark_state() == kLoadSucceeded);
        assert(kag.properties.at(L"loadBookMark").AsObjectNoAddRef() == &replacement);
    }
    kag.properties[L"loadBookMark"] = tTJSVariant(&load);
    balanced();

    request();
    {
        auto observer = kag.properties.at(L"loadBookMark");
        krkr2_host_cancel_bookmark_load();
        assert(krkr2_host_load_bookmark_state() == kBookmarkRejected);
        invoke(observer, 1999);
        assert(krkr2_host_load_bookmark_state() == kBookmarkRejected);
    }
    balanced();

    // Neither an entry error nor an observer installation error may leak the
    // pending owner, leave a false success, or bypass the framework loader.
    const int callsBeforeFailure = load.calls;
    for(int failure : {0, 1, 2}) {
        entry.callException = failure == 0 ? 1 : 0;
        entry.callStatus = failure == 1 ? TJS_E_FAIL : TJS_S_OK;
        kag.setStatus = failure == 2 ? TJS_E_FAIL : TJS_S_OK;
        request();
        assert(krkr2_host_load_bookmark_state() ==
            (failure == 0 ? kScriptException : kMethodUnavailable));
        assert(load.calls == callsBeforeFailure);
        balanced();
    }
    entry.callStatus = TJS_S_OK; kag.setStatus = TJS_S_OK;
    entry.function = false;
    request();
    assert(krkr2_host_load_bookmark_state() == kMethodUnavailable);
    assert(load.calls == callsBeforeFailure);
    balanced();
    entry.function = true;

    // A failing property writer must not retain the receiver or mask failure.
    // The remaining callable wrapper owns only the original function.
    for(bool throws : {false, true}) {
        request();
        kag.setThrows = throws;
        kag.setStatus = throws ? TJS_S_OK : TJS_E_FAIL;
        {
            auto observer = kag.properties.at(L"loadBookMark");
            assert(invoke(observer, 1999) == TJS_E_FAIL);
            assert(krkr2_host_load_bookmark_state() == kScriptException);
        }
        assert(!pending_load && kag.references == receiverReferences);
        kag.setThrows = false; kag.setStatus = TJS_S_OK;
        kag.properties[L"loadBookMark"] = tTJSVariant(&load);
        balanced();
    }

    // GetGlobal returns an owned reference, including when property lookup
    // fails or throws. Polling and save/load calls must release every lookup.
    assert(global.references == 1);
    global.properties.clear();
    kag.properties.clear();
    assert(kag.references == 1 && load.references == 1 && save.references == 1);
}
