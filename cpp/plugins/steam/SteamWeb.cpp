// Explicit browser local service for the krkrsteam.dll script protocol.
// This does not initialize Valve's native SDK or publish online achievements.
// See analysis/web-local-steam-service-four-binary-2026-10-30.md.
#define NCB_MODULE_NAME TJS_W("krkrsteam.dll")

#include "ncbind.hpp"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "tjsDictionary.h"

#include <boost/locale/encoding_utf.hpp>
#include <emscripten.h>
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

extern "C" EMSCRIPTEN_KEEPALIVE int krkr2_host_local_steam_service_abi() {
    return 1;
}

namespace {
using Bytes = std::vector<unsigned char>;
using Records = std::map<std::string, std::uint32_t>;
constexpr unsigned char StateMagic[] = {'K', 'I', 'R', 'I', '-', 'S', 'L', 0};
constexpr std::size_t MaximumState = 1024 * 1024;
constexpr std::size_t MaximumRecords = 4096;
constexpr std::size_t MaximumId = 1024;

std::string utf8(const ttstr &text) {
    return boost::locale::conv::utf_to_utf<char>(
        text.c_str(), text.c_str() + text.length(), boost::locale::conv::stop);
}
ttstr wide(const std::string &text) {
    const auto converted = boost::locale::conv::utf_to_utf<tjs_char>(
        text.data(), text.data() + text.size(), boost::locale::conv::stop);
    return ttstr(converted.data(), converted.size());
}
bool enabled() {
    return MAIN_THREAD_EM_ASM_INT({
        return Module['retromLocalSteamService'] === true ? 1 : 0;
    }) != 0;
}
void requireEnabled() {
    if(!enabled())
        TVPThrowExceptionMessage(TJS_W("KIRIKIRI_STEAM_LOCAL_SERVICE_DISABLED"));
}
void invalidState() {
    TVPThrowExceptionMessage(TJS_W("KIRIKIRI_STEAM_LOCAL_STATE_INVALID"));
}
bool validId(const std::string &id) {
    if(id.empty() || id.size() > MaximumId) return false;
    for(const unsigned char value : id)
        if(value < 32 || value == 127) return false;
    try {
        return utf8(wide(id)) == id;
    } catch(const std::exception &) {
        return false;
    }
}
void append32(Bytes &bytes, std::uint32_t value) {
    for(unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<unsigned char>(value >> shift));
}
std::uint32_t read32(const Bytes &bytes, std::size_t &position) {
    if(position > bytes.size() || bytes.size() - position < 4) invalidState();
    std::uint32_t value = 0;
    for(unsigned shift = 0; shift < 32; shift += 8)
        value |= std::uint32_t(bytes[position++]) << shift;
    return value;
}
Bytes encode(std::uint32_t app, const Records &records) {
    if(records.size() > MaximumRecords) invalidState();
    Bytes bytes(std::begin(StateMagic), std::end(StateMagic));
    append32(bytes, app);
    append32(bytes, static_cast<std::uint32_t>(records.size()));
    for(const auto &[id, timestamp] : records) {
        if(!validId(id) || bytes.size() + 8 + id.size() > MaximumState)
            invalidState();
        append32(bytes, static_cast<std::uint32_t>(id.size()));
        bytes.insert(bytes.end(), id.begin(), id.end());
        append32(bytes, timestamp);
    }
    return bytes;
}
Records decode(std::uint32_t app, const Bytes &bytes) {
    if(bytes.size() < sizeof(StateMagic) + 8 || bytes.size() > MaximumState ||
       !std::equal(std::begin(StateMagic), std::end(StateMagic), bytes.begin()))
        invalidState();
    std::size_t position = sizeof(StateMagic);
    if(read32(bytes, position) != app) invalidState();
    const auto count = read32(bytes, position);
    if(count > MaximumRecords) invalidState();
    Records records;
    for(std::uint32_t index = 0; index < count; ++index) {
        const auto length = read32(bytes, position);
        if(length == 0 || length > MaximumId || length > bytes.size() - position)
            invalidState();
        std::string id(bytes.begin() + position, bytes.begin() + position + length);
        position += length;
        const auto timestamp = read32(bytes, position);
        if(!validId(id) || !records.emplace(std::move(id), timestamp).second)
            invalidState();
    }
    if(position != bytes.size()) invalidState();
    return records;
}

class LocalService {
    std::uint32_t app_ = 0;
    bool ready_ = false;
    Records records_;

    ttstr path() const {
        return TVPDataPath + TJS_W("steam-local/") +
               wide(std::to_string(app_)) + TJS_W(".dat");
    }
    void publish(const Bytes &bytes) const {
        const auto native = TVPGetLocallyAccessibleName(path());
        if(native.IsEmpty())
            TVPThrowExceptionMessage(TJS_W("KIRIKIRI_STEAM_LOCAL_STORAGE_UNAVAILABLE"));
        const auto name = utf8(native);
        // Publish a complete file, preserving the original TVP managed-save
        // namespace. No transient truncate or game-progress witness is emitted.
        const auto success = MAIN_THREAD_EM_ASM_INT({
            try {
                if(typeof VLFS === 'undefined' ||
                   typeof VLFS.onWriteClose !== 'function' ||
                   typeof VLFS.registerOverlayFile !== 'function') return 0;
                var path = UTF8ToString($0);
                var bytes = HEAPU8.slice($1, $1 + $2);
                VLFS.onWriteClose(path, bytes);
                VLFS.registerOverlayFile(path, bytes);
                return 1;
            } catch(error) {
                console.error('[steam-local] managed state publication failed', error);
                return 0;
            }
        }, name.c_str(), bytes.data(), bytes.size());
        if(!success)
            TVPThrowExceptionMessage(TJS_W("KIRIKIRI_STEAM_LOCAL_STATE_WRITE_FAILED"));
        TVPClearStorageCaches();
    }
public:
    bool ready() const { return ready_; }
    const Records &records() const { return records_; }
    void reset() {
        ready_ = false;
        app_ = 0;
        records_.clear();
    }
    void select(std::uint32_t app) {
        if(ready_ && app != app_)
            TVPThrowExceptionMessage(TJS_W("KIRIKIRI_STEAM_LOCAL_APP_ALREADY_INITIALIZED"));
        app_ = app;
    }
    bool initialize() {
        if(ready_) return true;
        if(app_ == 0)
            TVPThrowExceptionMessage(TJS_W("KIRIKIRI_STEAM_LOCAL_APP_ID_REQUIRED"));
        Records loaded;
        if(TVPIsExistentStorageNoSearch(path())) {
            std::unique_ptr<tTJSBinaryStream> input(TVPCreateStream(path(), TJS_BS_READ));
            const auto length = input->GetSize();
            if(length > MaximumState) invalidState();
            Bytes bytes(static_cast<std::size_t>(length));
            input->ReadBuffer(bytes.data(), bytes.size());
            loaded = decode(app_, bytes);
        } else {
            publish(encode(app_, loaded));
        }
        records_ = std::move(loaded);
        ready_ = true;
        return true;
    }
    bool update(const std::string &id, bool achieved) {
        if(!ready_ || !validId(id)) return false;
        const auto existing = records_.find(id);
        if(!achieved && existing == records_.end()) return false;
        if(existing != records_.end() && (existing->second != 0) == achieved)
            return true;
        std::uint32_t timestamp = 0;
        if(achieved) {
            const auto now = std::time(nullptr);
            if(now <= 0 || std::uint64_t(now) > std::numeric_limits<std::uint32_t>::max())
                TVPThrowExceptionMessage(TJS_W("KIRIKIRI_STEAM_LOCAL_TIME_UNAVAILABLE"));
            timestamp = static_cast<std::uint32_t>(now);
        }
        auto changed = records_;
        changed[id] = timestamp;
        publish(encode(app_, changed));
        records_ = std::move(changed);
        return true;
    }
};
LocalService service;
void resetService() { service.reset(); }
tTVPAtExit resetAtExit(TVP_ATEXIT_PRI_PREPARE, resetService);

std::string achievementId(const tTJSVariant &value) {
    if(value.Type() == tvtString) {
        try { return utf8(ttstr(value)); }
        catch(const std::exception &) { return {}; }
    }
    if(value.Type() == tvtInteger) {
        const auto index = value.AsInteger();
        if(index < 0 || std::uint64_t(index) >= service.records().size()) return {};
        auto record = service.records().begin();
        std::advance(record, index);
        return record->first;
    }
    return {};
}
void member(iTJSDispatch2 *object, const tjs_char *name, const tTJSVariant &value) {
    if(TJS_FAILED(object->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, object)))
        TVPThrowExceptionMessage(TJS_W("KIRIKIRI_STEAM_LOCAL_RESULT_FAILED"));
}

class Steam {
public:
    static tjs_error restartAppIfNecessary(tTJSVariant *result, tjs_int count,
                                           tTJSVariant **arguments, iTJSDispatch2 *) {
        requireEnabled();
        if(count < 1) return TJS_E_BADPARAMCOUNT;
        if(arguments[0]->Type() != tvtInteger || arguments[0]->AsInteger() <= 0 ||
           std::uint64_t(arguments[0]->AsInteger()) > std::numeric_limits<std::uint32_t>::max())
            TVPThrowExceptionMessage(TJS_W("KIRIKIRI_STEAM_LOCAL_APP_ID_INVALID"));
        service.select(static_cast<std::uint32_t>(arguments[0]->AsInteger()));
        if(result) *result = tjs_int(0); // No process restart for the local backend.
        return TJS_S_OK;
    }
    static tjs_error initializeSteamAPI(tTJSVariant *result, tjs_int,
                                        tTJSVariant **, iTJSDispatch2 *) {
        requireEnabled();
        const auto initialized = service.initialize();
        if(result) *result = tjs_int(initialized);
        return TJS_S_OK;
    }
    static tjs_error requestInitialize(tTJSVariant *result, tjs_int,
                                       tTJSVariant **, iTJSDispatch2 *) {
        requireEnabled();
        if(result) *result = tjs_int(service.ready());
        return TJS_S_OK;
    }
    static int initialized() { return enabled() && service.ready(); }
    static int achievementsCount() {
        return initialized() ? static_cast<int>(service.records().size()) : 0;
    }
    static tjs_error getAchievement(tTJSVariant *result, tjs_int count,
                                    tTJSVariant **arguments, iTJSDispatch2 *) {
        if(count < 1) return TJS_E_BADPARAMCOUNT;
        if(result) result->Clear();
        if(!initialized()) return TJS_S_OK;
        const auto id = achievementId(*arguments[0]);
        const auto record = service.records().find(id);
        if(record == service.records().end() || !result) return TJS_S_OK;
        auto *object = TJSCreateDictionaryObject();
        try {
            member(object, TJS_W("ach"), tTJSVariant(wide(id)));
            member(object, TJS_W("achieved"), tTJSVariant(tjs_int(record->second != 0)));
            member(object, TJS_W("unlockTime"), tTJSVariant(tjs_int64(record->second)));
            member(object, TJS_W("name"), tTJSVariant(TJS_W("")));
            member(object, TJS_W("desc"), tTJSVariant(TJS_W("")));
            *result = tTJSVariant(object, object);
        } catch(...) {
            object->Release();
            throw;
        }
        object->Release();
        return TJS_S_OK;
    }
    static tjs_error changeAchievement(tTJSVariant *result, tjs_int count,
                                       tTJSVariant **arguments, bool achieved) {
        if(count < 1) return TJS_E_BADPARAMCOUNT;
        const auto changed = initialized() && service.update(achievementId(*arguments[0]), achieved);
        if(result) *result = tjs_int(changed);
        return TJS_S_OK;
    }
    static tjs_error setAchievement(tTJSVariant *result, tjs_int count,
                                    tTJSVariant **arguments, iTJSDispatch2 *) {
        return changeAchievement(result, count, arguments, true);
    }
    static tjs_error clearAchievement(tTJSVariant *result, tjs_int count,
                                      tTJSVariant **arguments, iTJSDispatch2 *) {
        return changeAchievement(result, count, arguments, false);
    }
    static tjs_error triggerScreenshot(tTJSVariant *, tjs_int,
                                       tTJSVariant **, iTJSDispatch2 *) {
        TVPThrowExceptionMessage(TJS_W("KIRIKIRI_STEAM_SCREENSHOT_UNSUPPORTED"));
        return TJS_E_FAIL;
    }
};
}

NCB_REGISTER_CLASS(Steam) {
    RawCallback("restartAppIfNecessary", &Steam::restartAppIfNecessary, TJS_STATICMEMBER);
    RawCallback("initializeSteamAPI", &Steam::initializeSteamAPI, TJS_STATICMEMBER);
    RawCallback("requestInitialize", &Steam::requestInitialize, TJS_STATICMEMBER);
    Property("initialized", &Steam::initialized, int());
    Property("achievementsCount", &Steam::achievementsCount, int());
    RawCallback("getAchievement", &Steam::getAchievement, TJS_STATICMEMBER);
    RawCallback("setAchievement", &Steam::setAchievement, TJS_STATICMEMBER);
    RawCallback("clearAchievement", &Steam::clearAchievement, TJS_STATICMEMBER);
    RawCallback("triggerScreenshot", &Steam::triggerScreenshot, TJS_STATICMEMBER);
}
