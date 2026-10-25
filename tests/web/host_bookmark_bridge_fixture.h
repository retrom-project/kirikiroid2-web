// Shared Web host-boundary dependencies. Tests include the actual bridge; this
// fixture supplies TJS dispatch ownership and the event/JavaScript boundaries.
#pragma once
#include <cassert>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
constexpr int TJS_E_MEMBERNOTFOUND = -1;
constexpr int TJS_MEMBERENSURE = 0x200;
constexpr int TJS_IGNOREPROP = 0x100;
enum { tvtVoid, tvtObject, tvtString, tvtInteger };
struct iTJSDispatch2;
void retain(iTJSDispatch2 *);
void release(iTJSDispatch2 *);
struct TestString {
    std::wstring value;
    int GetLength() const { return value.size(); }
    operator const wchar_t *() const { return value.c_str(); }
};
struct tTJSVariant;
struct tTJSVariantClosure {
    iTJSDispatch2 *Object;
    iTJSDispatch2 *ObjThis;
    tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *,
                       tjs_int, tTJSVariant **, iTJSDispatch2 *) const;
};
struct tTJSVariant {
    int type = tvtVoid;
    iTJSDispatch2 *object = nullptr;
    iTJSDispatch2 *objectThis = nullptr;
    TestString string;
    int integer = 0;
    tTJSVariant() = default;
    explicit tTJSVariant(int value) : type(tvtInteger), integer(value) {}
    explicit tTJSVariant(iTJSDispatch2 *value, iTJSDispatch2 *receiver = nullptr)
        : type(tvtObject), object(value), objectThis(receiver) { retain(object); retain(objectThis); }
    explicit tTJSVariant(const wchar_t *value) : type(tvtString), string{value} {}
    tTJSVariant(const tTJSVariant &value)
        : type(value.type), object(value.object), objectThis(value.objectThis), string(value.string), integer(value.integer) {
        retain(object); retain(objectThis);
    }
    ~tTJSVariant() { release(object); release(objectThis); }
    tTJSVariant &operator=(const tTJSVariant &value) {
        retain(value.object); retain(value.objectThis);
        release(object); release(objectThis);
        type = value.type; object = value.object; objectThis = value.objectThis; string = value.string; integer = value.integer;
        return *this;
    }
    int Type() const { return type; }
    iTJSDispatch2 *AsObjectNoAddRef() const { return object; }
    iTJSDispatch2 *AsObjectThisNoAddRef() const { return objectThis; }
    tTJSVariantClosure AsObjectClosureNoAddRef() const { return {object, objectThis}; }
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
    int lastGetFlags = 0;
    int lastSetFlags = 0;
    bool setThrows = false;
    int countStatus = TJS_S_OK;
    int callStatus = TJS_S_OK;
    int callResult = 1;
    iTJSDispatch2 *lastReceiver = nullptr;
    std::function<void(tTJSVariant *, int, tTJSVariant **)> onCall;
    std::function<void(const wchar_t *)> onGet;
    int PropGet(int flags, const wchar_t *name, void *, tTJSVariant *result,
                iTJSDispatch2 *) {
        lastGetFlags = flags;
        if(throws) throw std::runtime_error("script unavailable");
        if(onGet) onGet(name);
        auto found = properties.find(name);
        if(found == properties.end()) return -1;
        *result = found->second;
        return TJS_S_OK;
    }
    int PropSet(int flags, const wchar_t *name, void *, const tTJSVariant *value, iTJSDispatch2 *) {
        lastSetFlags = flags;
        if(setThrows) throw std::runtime_error("property write failed");
        if(setStatus != TJS_S_OK) return setStatus;
        properties[name] = *value;
        return TJS_S_OK;
    }
    int GetCount(tjs_int *result, const wchar_t *, tjs_uint32 *, iTJSDispatch2 *) {
        *result = properties.size();
        return countStatus;
    }
    virtual int IsInstanceOf(tjs_uint32, const tjs_char *, tjs_uint32 *, const wchar_t *, iTJSDispatch2 *) {
        return function ? TJS_S_TRUE : TJS_S_FALSE;
    }
    virtual int FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *result, int count,
                 tTJSVariant **arguments, iTJSDispatch2 *receiver) {
        if(callException == 1) throw std::runtime_error("bookmark test failure");
        if(callException == 2) throw 7;
        assert(count == 0 || count == 1);
        ++calls;
        lastReceiver = receiver;
        slot = count ? arguments[0]->integer : -1;
        *result = tTJSVariant(callResult);
        if(onCall) onCall(result, count, arguments);
        return callStatus;
    }
};
inline tjs_error tTJSVariantClosure::FuncCall(tjs_uint32 flag, const tjs_char *name,
    tjs_uint32 *hint, tTJSVariant *result, tjs_int count, tTJSVariant **params, iTJSDispatch2 *receiver) const {
    return Object->FuncCall(flag, name, hint, result, count, params,
        ObjThis ? ObjThis : (receiver ? receiver : Object));
}
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
#define EM_JS(type, name, args, ...) type name args;
inline int krkr2_host_main_loop_is_idle() {
    return testLoopRegistered && !testLoopPending && !testLoopStopping;
}
struct TestGameSaveEvent { int phase; int id; bool success; };
std::vector<TestGameSaveEvent> testGameSaveEvents;
std::function<void(const TestGameSaveEvent &)> testGameSaveCallback;
inline void krkr2_host_notify_game_save(int phase, int id, int success) {
    const TestGameSaveEvent event{phase, id, success != 0};
    testGameSaveEvents.push_back(event);
    // EM_JS catches Host callback failures; the original TJS call must survive.
    try { if(testGameSaveCallback) testGameSaveCallback(event); } catch(...) {}
}
