// Exercise the production Web boundary with minimal NCB containers. This tests
// geometry/result construction, not TJS dispatch or reference counting; the
// existing real-game checkpoint replay covers the actual engine integration.
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

using tjs_int = std::int32_t;
using tjs_int32 = std::int32_t;
using tjs_int64 = std::int64_t;
using tjs_error = int;
#define TJS_W(value) L##value
constexpr int TJS_S_OK = 0, TJS_E_FAIL = -1;
constexpr int TJS_E_BADPARAMCOUNT = -2, TJS_E_INVALIDPARAM = -3;

struct iTJSDispatch2;
struct tTJSVariant {
    tjs_int64 integer = 0;
    std::wstring string;
    iTJSDispatch2 *object = nullptr;
    tTJSVariant() = default;
    tTJSVariant(tjs_int64 value) : integer(value) {}
    tTJSVariant(const wchar_t *value) : string(value) {}
    tTJSVariant(iTJSDispatch2 *value) : object(value) {}
    explicit operator tjs_int64() const { return integer; }
    void SetObject(iTJSDispatch2 *value, iTJSDispatch2 *) { object = value; }
};
struct iTJSDispatch2 {
    bool array = false;
    std::map<std::wstring, tTJSVariant> fields;
    std::map<tjs_int32, tTJSVariant> items;
};
std::vector<std::unique_ptr<iTJSDispatch2>> arena;
bool failCreate = false, failWrite = false;
struct ncbPropAccessor {
    iTJSDispatch2 *object = nullptr;
    ncbPropAccessor(bool array) {
        if(failCreate) return;
        arena.push_back(std::make_unique<iTJSDispatch2>());
        object = arena.back().get();
        object->array = array;
    }
    bool IsValid() const { return object != nullptr; }
    iTJSDispatch2 *GetDispatch() const { return object; }
    template <typename T> bool SetValue(const wchar_t *key, T value) {
        if(failWrite) return false;
        object->fields[key] = tTJSVariant(value);
        return true;
    }
    bool SetValue(tjs_int32 index, iTJSDispatch2 *value) {
        if(failWrite) return false;
        object->items[index] = tTJSVariant(value);
        return true;
    }
};
struct ncbArrayAccessor : ncbPropAccessor {
    ncbArrayAccessor() : ncbPropAccessor(true) {}
};
struct ncbDictionaryAccessor : ncbPropAccessor {
    ncbDictionaryAccessor() : ncbPropAccessor(false) {}
};
bool screenAvailable = true;
tjs_int screenWidth = 2048, screenHeight = 1280;
bool TVPGetScreenSize(tjs_int index, tjs_int &width, tjs_int &height) {
    assert(index == 0);
    width = screenWidth;
    height = screenHeight;
    return screenAvailable;
}

#include "cpp/plugins/WebDisplayMonitors.h"

iTJSDispatch2 *query(std::initializer_list<tjs_int64> values) {
    std::vector<tTJSVariant> arguments;
    for(auto value : values) arguments.emplace_back(value);
    std::vector<tTJSVariant *> pointers;
    for(auto &value : arguments) pointers.push_back(&value);
    tTJSVariant result;
    assert(WindowExWeb::getDisplayMonitors(
        &result, pointers.size(), pointers.data()) == TJS_S_OK);
    assert(result.object && result.object->array);
    return result.object;
}
void rect(iTJSDispatch2 *value, tjs_int x, tjs_int y, tjs_int w, tjs_int h) {
    assert(value && !value->array);
    assert(value->fields.size() == 4);
    assert(value->fields.at(L"x").integer == x);
    assert(value->fields.at(L"y").integer == y);
    assert(value->fields.at(L"w").integer == w);
    assert(value->fields.at(L"h").integer == h);
}
int main() {
    const auto all = query({});
    assert(all->items.size() == 1);
    const auto info = all->items.at(0).object;
    assert(info && info->fields.size() == 5);
    assert(info->fields.at(L"name").string == L"WebViewport");
    assert(info->fields.at(L"primary").integer == 1);
    for(const auto key : {L"monitor", L"work", L"intersect"})
        rect(info->fields.at(key).object, 0, 0, 2048, 1280);
    rect(query({-20, 10, 100, 50})->items.at(0).object->fields.at(L"intersect").object,
         0, 10, 80, 50);
    rect(query({2040, 1270, 100, 100})->items.at(0).object->fields.at(L"intersect").object,
         2040, 1270, 8, 10);
    // Persisted window bounds, disjoint rectangles, edge contact and empty area.
    assert(query({0, 0, 1280, 720})->items.size() == 1);
    for(const auto values : {
            std::initializer_list<tjs_int64>{2048, 0, 100, 100},
            {0, 1280, 100, 100}, {-100, 0, 100, 100}, {0, 0, 0, 10},
            {0, 0, 10, -1}, {2147483647, 0, 2147483647, 100}})
        assert(query(values)->items.empty());
    tTJSVariant result, huge(2147483648LL), zero(tjs_int64{0});
    tTJSVariant *parameters[] = {&huge, &zero, &zero, &zero};
    for(const auto count : {1, 2, 3, 5})
        assert(WindowExWeb::getDisplayMonitors(&result, count, parameters) == TJS_E_BADPARAMCOUNT);
    assert(WindowExWeb::getDisplayMonitors(&result, 4, parameters) == TJS_E_INVALIDPARAM);
    assert(WindowExWeb::getDisplayMonitors(nullptr, 0, nullptr) == TJS_S_OK);
    screenAvailable = false;
    assert(WindowExWeb::getDisplayMonitors(&result, 0, nullptr) == TJS_E_FAIL);
    screenAvailable = true;
    screenHeight = 0;
    assert(WindowExWeb::getDisplayMonitors(&result, 0, nullptr) == TJS_E_FAIL);
    screenHeight = 1280;
    failCreate = true;
    assert(WindowExWeb::getDisplayMonitors(&result, 0, nullptr) == TJS_E_FAIL);
    failCreate = false;
    failWrite = true;
    assert(WindowExWeb::getDisplayMonitors(&result, 0, nullptr) == TJS_E_FAIL);
}
