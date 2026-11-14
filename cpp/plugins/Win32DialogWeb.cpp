#define NCB_MODULE_NAME TJS_W("win32dialog.dll")
#include "ncbind.hpp"
#include "EventIntf.h"
#include "tjsDictionary.h"
#include <boost/locale/encoding_utf.hpp>
#include <emscripten.h>
#include <json/document.h>
#include <json/stringbuffer.h>
#include <json/writer.h>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>

#include "Win32DialogWeb.inc"

namespace {
using Json = rapidjson::Value;
using Allocator = rapidjson::Document::AllocatorType;

std::string utf8(const ttstr &text) {
    return boost::locale::conv::utf_to_utf<char>(
        text.c_str(), text.c_str() + text.length());
}
ttstr wide(const std::string &text) {
    auto chars = boost::locale::conv::utf_to_utf<tjs_char>(
        text.data(), text.data() + text.size());
    return ttstr(chars.data(), chars.size());
}

// Only the declared primitive template/rectangle fields cross the DOM boundary.
// A Header/Items object can be invalidated immediately after makeTemplate.
Json argument(const tTJSVariant &value, Allocator &allocator) {
    if(value.Type() == tvtInteger) {
        Json result;
        result.SetInt64(value.AsInteger());
        return result;
    }
    if(value.Type() == tvtReal) {
        Json result;
        result.SetDouble(value.AsReal());
        return result;
    }
    if(value.Type() == tvtString) {
        const auto text = utf8(ttstr(value));
        return Json(text.data(), text.size(), allocator);
    }
    if(value.Type() != tvtObject || !value.AsObjectNoAddRef()) return Json();
    static const char *fields[] = {
        "helpID", "exStyle", "style", "dlgItems", "x", "y", "cx", "cy",
        "menu", "windowClass", "title", "pointSize", "weight", "italic",
        "charset", "typeFace", "id", "left", "top", "right", "bottom",
        "flags", "showCmd", "minLeft", "minTop", "maxLeft", "maxTop",
        "normalLeft", "normalTop", "normalRight", "normalBottom"
    };
    Json result(rapidjson::kObjectType);
    auto *object = value.AsObjectNoAddRef();
    for(const char *field : fields) {
        tTJSVariant member;
        const auto key = wide(field);
        if(TJS_SUCCEEDED(object->PropGet(0, key.c_str(), nullptr, &member, object)) &&
           (member.Type() == tvtInteger || member.Type() == tvtReal ||
            member.Type() == tvtString))
            result.AddMember(Json(field, allocator), argument(member, allocator), allocator);
    }
    return result;
}

tTJSVariant resultValue(const Json &value) {
    if(value.IsNull()) return tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr), nullptr);
    if(value.IsBool()) return tTJSVariant(static_cast<tjs_int>(value.GetBool()));
    if(value.IsInt64()) return tTJSVariant(static_cast<tjs_int64>(value.GetInt64()));
    if(value.IsNumber()) return tTJSVariant(value.GetDouble());
    if(value.IsString()) return tTJSVariant(wide({value.GetString(), value.GetStringLength()}));
    auto *dictionary = TJSCreateDictionaryObject();
    tTJSVariant result(dictionary, dictionary);
    dictionary->Release();
    for(auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
        const auto key = wide({member->name.GetString(), member->name.GetStringLength()});
        auto item = resultValue(member->value);
        dictionary->PropSet(TJS_MEMBERENSURE, key.c_str(), nullptr, &item, dictionary);
    }
    return result;
}

struct WIN32DialogWeb {
    static std::map<int, WIN32DialogWeb *> live;
    static int nextId;
    iTJSDispatch2 *owner; // Borrowed; the script instance owns this NCB adaptor.
    int id;
    tTJSVariant iconValue;

    explicit WIN32DialogWeb(iTJSDispatch2 *object) : owner(object), id(++nextId) {
        live.emplace(id, this);
    }
    ~WIN32DialogWeb() {
        live.erase(id);
        TVPWebDialogDispose(id);
    }
    static WIN32DialogWeb *get(iTJSDispatch2 *owner) {
        auto *instance = ncbInstanceAdaptor<WIN32DialogWeb>::GetNativeInstance(owner);
        if(!instance) {
            instance = new WIN32DialogWeb(owner);
            if(!ncbInstanceAdaptor<WIN32DialogWeb>::SetAdaptorWithNativeInstance(owner, instance)) {
                delete instance;
                TVPThrowExceptionMessage(TJS_W("Cannot attach Web dialog instance"));
            }
        }
        return instance;
    }

    tjs_error command(const char *operation, tTJSVariant *result,
                      tjs_int count, tTJSVariant **params) {
        rapidjson::Document request(rapidjson::kObjectType);
        auto &allocator = request.GetAllocator();
        Json args(rapidjson::kArrayType);
        for(tjs_int i = 0; i < count; ++i)
            args.PushBack(argument(*params[i], allocator), allocator);
        request.AddMember("args", args, allocator);
        if(std::string(operation) == "open") {
            tTJSVariant modeless;
            owner->PropGet(0, TJS_W("modeless"), nullptr, &modeless, owner);
            request.AddMember("modeless", modeless.operator bool(), allocator);
        }
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        request.Accept(writer);
        std::unique_ptr<char, decltype(&std::free)> response(
            TVPWebDialogCommand(id, operation, buffer.GetString()), &std::free);
        rapidjson::Document decoded;
        decoded.Parse(response.get());
        if(decoded.HasParseError() || !decoded.IsObject())
            TVPThrowExceptionMessage(TJS_W("Invalid Web dialog response"));
        if(decoded.HasMember("error"))
            TVPThrowExceptionMessage(wide(decoded["error"].GetString()).c_str());
        if(result) {
            result->Clear();
            if(decoded.HasMember("value")) *result = resultValue(decoded["value"]);
        }
        if(std::string(operation) == "open") {
            tTJSVariant args[3] = {272, 0, id};
            tTJSVariant *pointers[] = {args, args + 1, args + 2};
            const auto status = owner->FuncCall(
                0, TJS_W("onInit"), nullptr, nullptr, 3, pointers, owner);
            if(TJS_FAILED(status)) return status;
        }
        return TJS_S_OK;
    }

#define WEB_DIALOG_METHOD(name, required) \
    static tjs_error name(tTJSVariant *result, tjs_int count, tTJSVariant **params, iTJSDispatch2 *owner) { \
        if(count < required) return TJS_E_BADPARAMCOUNT; \
        return get(owner)->command(#name, result, count, params); \
    }
    WEB_DIALOG_METHOD(makeTemplate, 1)
    WEB_DIALOG_METHOD(open, 0)
    WEB_DIALOG_METHOD(close, 0)
    WEB_DIALOG_METHOD(getItem, 1)
    WEB_DIALOG_METHOD(getItemID, 1)
    WEB_DIALOG_METHOD(getItemClassName, 1)
    WEB_DIALOG_METHOD(setItemText, 2)
    WEB_DIALOG_METHOD(getItemText, 1)
    WEB_DIALOG_METHOD(setItemInt, 2)
    WEB_DIALOG_METHOD(getItemInt, 1)
    WEB_DIALOG_METHOD(setItemLong, 3)
    WEB_DIALOG_METHOD(getItemLong, 2)
    WEB_DIALOG_METHOD(setItemEnabled, 2)
    WEB_DIALOG_METHOD(getItemEnabled, 1)
    WEB_DIALOG_METHOD(setItemFocus, 1)
    WEB_DIALOG_METHOD(setItemPos, 3)
    WEB_DIALOG_METHOD(setItemSize, 3)
    WEB_DIALOG_METHOD(getItemLeft, 1)
    WEB_DIALOG_METHOD(getItemTop, 1)
    WEB_DIALOG_METHOD(getItemWidth, 1)
    WEB_DIALOG_METHOD(getItemHeight, 1)
    WEB_DIALOG_METHOD(sendItemMessage, 2)
    WEB_DIALOG_METHOD(setPos, 2)
    WEB_DIALOG_METHOD(setSize, 2)
    WEB_DIALOG_METHOD(getWindowRect, 0)
    WEB_DIALOG_METHOD(getClientRect, 0)
    WEB_DIALOG_METHOD(mapRect, 1)
    WEB_DIALOG_METHOD(getPlacement, 0)
    WEB_DIALOG_METHOD(setPlacement, 1)
    WEB_DIALOG_METHOD(maximize, 0)
    WEB_DIALOG_METHOD(restoreMaximize, 0)
    WEB_DIALOG_METHOD(setActive, 0)
    WEB_DIALOG_METHOD(show, 1)
    WEB_DIALOG_METHOD(invalidateAll, 0)
#undef WEB_DIALOG_METHOD

#define WEB_DIALOG_PROPERTY(name) \
    static tjs_error name(tTJSVariant *result, tjs_int, tTJSVariant **, iTJSDispatch2 *owner) { \
        return get(owner)->command(#name, result, 0, nullptr); \
    }
    WEB_DIALOG_PROPERTY(left)
    WEB_DIALOG_PROPERTY(top)
    WEB_DIALOG_PROPERTY(width)
    WEB_DIALOG_PROPERTY(height)
    WEB_DIALOG_PROPERTY(HWND)
    WEB_DIALOG_PROPERTY(isValid)
#undef WEB_DIALOG_PROPERTY

    static tjs_error getIcon(tTJSVariant *result, tjs_int,
                             tTJSVariant **, iTJSDispatch2 *owner) {
        if(result) *result = get(owner)->iconValue;
        return TJS_S_OK;
    }
    static tjs_error setIcon(tTJSVariant *result, tjs_int count,
                             tTJSVariant **params, iTJSDispatch2 *owner) {
        if(count < 1) return TJS_E_BADPARAMCOUNT;
        const auto &value = *params[0];
        if(value.Type() != tvtVoid &&
           !(value.Type() == tvtObject && !value.AsObjectNoAddRef()) &&
           !(value.Type() == tvtInteger && value.AsInteger() == 0))
            TVPThrowExceptionMessage(TJS_W("Native GDI dialog icons are not supported in the browser"));
        get(owner)->iconValue.Clear();
        if(result) result->Clear();
        return TJS_S_OK;
    }

    // The original DLL's default virtual message handlers return false.
    static tjs_error defaultEvent(tTJSVariant *result, tjs_int,
                                  tTJSVariant **, iTJSDispatch2 *) {
        if(result) *result = static_cast<tjs_int>(0);
        return TJS_S_OK;
    }
};
std::map<int, WIN32DialogWeb *> WIN32DialogWeb::live;
int WIN32DialogWeb::nextId = 0;
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void krkr2_web_dialog_event(
    int id, int kind, int wparam, int lparam) {
    const auto found = WIN32DialogWeb::live.find(id);
    if(found == WIN32DialogWeb::live.end()) return;
    auto *owner = found->second->owner;
    if(!TJSIsObjectValid(owner->IsValid(0, nullptr, nullptr, owner))) return;
    ttstr event(kind == 1 ? TJS_W("onSize") : TJS_W("onCommand"));
    tTJSVariant args[3] = {kind == 1 ? 5 : 273, wparam, lparam};
    TVPPostEvent(owner, owner, event, 0, TVP_EPT_POST, 3, args);
}

NCB_GET_INSTANCE_HOOK(WIN32DialogWeb) {
    NCB_INSTANCE_GETTER(objthis) { return ClassT::get(objthis); }
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
};

NCB_ATTACH_CLASS_WITH_HOOK(WIN32DialogWeb, WIN32Dialog) {
#define REGISTER_METHOD(name) NCB_METHOD_RAW_CALLBACK(name, &WIN32DialogWeb::name, 0)
    REGISTER_METHOD(makeTemplate);
    REGISTER_METHOD(open);
    REGISTER_METHOD(close);
    REGISTER_METHOD(getItem);
    REGISTER_METHOD(getItemID);
    REGISTER_METHOD(getItemClassName);
    REGISTER_METHOD(setItemText);
    REGISTER_METHOD(getItemText);
    REGISTER_METHOD(setItemInt);
    REGISTER_METHOD(getItemInt);
    REGISTER_METHOD(setItemLong);
    REGISTER_METHOD(getItemLong);
    REGISTER_METHOD(setItemEnabled);
    REGISTER_METHOD(getItemEnabled);
    REGISTER_METHOD(setItemFocus);
    REGISTER_METHOD(setItemPos);
    REGISTER_METHOD(setItemSize);
    REGISTER_METHOD(getItemLeft);
    REGISTER_METHOD(getItemTop);
    REGISTER_METHOD(getItemWidth);
    REGISTER_METHOD(getItemHeight);
    REGISTER_METHOD(sendItemMessage);
    REGISTER_METHOD(setPos);
    REGISTER_METHOD(setSize);
    REGISTER_METHOD(getWindowRect);
    REGISTER_METHOD(getClientRect);
    REGISTER_METHOD(mapRect);
    REGISTER_METHOD(getPlacement);
    REGISTER_METHOD(setPlacement);
    REGISTER_METHOD(maximize);
    REGISTER_METHOD(restoreMaximize);
    REGISTER_METHOD(setActive);
    REGISTER_METHOD(show);
    REGISTER_METHOD(invalidateAll);
#undef REGISTER_METHOD
#define REGISTER_PROPERTY(name) NCB_PROPERTY_RAW_CALLBACK_RO(name, &WIN32DialogWeb::name, 0)
    REGISTER_PROPERTY(left);
    REGISTER_PROPERTY(top);
    REGISTER_PROPERTY(width);
    REGISTER_PROPERTY(height);
    REGISTER_PROPERTY(HWND);
    REGISTER_PROPERTY(isValid);
#undef REGISTER_PROPERTY
    RawCallback(TJS_W("icon"), &WIN32DialogWeb::getIcon,
                 &WIN32DialogWeb::setIcon, 0);
    RawCallback(TJS_W("onInit"), &WIN32DialogWeb::defaultEvent, 0);
    RawCallback(TJS_W("onCommand"), &WIN32DialogWeb::defaultEvent, 0);
    RawCallback(TJS_W("onSize"), &WIN32DialogWeb::defaultEvent, 0);
    RawCallback(TJS_W("onNotify"), &WIN32DialogWeb::defaultEvent, 0);
    RawCallback(TJS_W("onHScroll"), &WIN32DialogWeb::defaultEvent, 0);
    RawCallback(TJS_W("onVScroll"), &WIN32DialogWeb::defaultEvent, 0);
}
