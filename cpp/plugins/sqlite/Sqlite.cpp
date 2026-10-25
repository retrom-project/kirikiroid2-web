// Web platform implementation of the Sqlite/SqliteStatement script surface.
// This uses the real SQLite engine and a VLFS-backed pager. It is an explicit
// browser extension, not a reconstruction of the four native references.
#include "ncbind.hpp"
#include "StorageIntf.h"
#include "Database.h"
#include <boost/locale/encoding_utf.hpp>
#include <emscripten.h>

#define NCB_MODULE_NAME TJS_W("sqlite3.dll")

namespace {

std::string utf8(const ttstr &text) {
    return boost::locale::conv::utf_to_utf<char>(text.c_str(), text.c_str() + text.length());
}
ttstr wide(const std::string &text) {
    const auto converted = boost::locale::conv::utf_to_utf<tjs_char>(text.data(), text.data() + text.size());
    return ttstr(converted.data(), converted.size());
}

class TvpFile : public KirikiriSqlite::StorageFile {
    std::unique_ptr<tTJSBinaryStream> stream_;
public:
    explicit TvpFile(const std::string &path) : stream_(TVPCreateStream(wide(path), TJS_BS_READ)) {
        if(!stream_) throw KirikiriSqlite::StorageError(SQLITE_CANTOPEN, "Cannot open Kiri SQLite storage");
    }
    int read(void *out, int amount, sqlite3_int64 offset) override {
        if(stream_->Seek(offset, TJS_BS_SEEK_SET) != static_cast<tjs_uint64>(offset))
            throw KirikiriSqlite::StorageError(SQLITE_IOERR_SEEK, "Cannot seek Kiri SQLite storage");
        return static_cast<int>(stream_->Read(out, amount));
    }
    void write(const void *, int, sqlite3_int64) override {
        throw KirikiriSqlite::StorageError(SQLITE_READONLY, "Packaged SQLite resources are read-only");
    }
    void truncate(sqlite3_int64) override {
        throw KirikiriSqlite::StorageError(SQLITE_READONLY, "Packaged SQLite resources are read-only");
    }
    void sync() override {}
    sqlite3_int64 size() override { return static_cast<sqlite3_int64>(stream_->GetSize()); }
};

class TvpBacking : public KirikiriSqlite::Backing {
public:
    std::string canonical(const std::string &path) override {
        if(path.empty()) throw KirikiriSqlite::StorageError(SQLITE_CANTOPEN, "SQLite storage path is empty");
        const auto input = wide(path);
        const auto placed = TVPGetPlacedPath(input);
        return utf8(placed.IsEmpty() ? TVPNormalizeStorageName(input) : placed);
    }
    bool exists(const std::string &path) override { return TVPIsExistentStorageNoSearch(wide(path)); }
    std::unique_ptr<KirikiriSqlite::StorageFile> read(const std::string &path) override {
        return std::make_unique<TvpFile>(path);
    }
    void publish(const std::string &path, const KirikiriSqlite::Bytes &bytes) override {
        // Copy before publishing: the Wasm buffer is not retained by JS. The
        // same complete bytes enter VLFS and its existing save-close hook, so
        // host checkpoints can restore a fresh core, including journal deletes.
        const int status = MAIN_THREAD_EM_ASM_INT({
            try {
                if(typeof VLFS === 'undefined' || typeof VLFS.onWriteClose !== 'function') return 0;
                var path = UTF8ToString($0);
                var data = HEAPU8.slice($1, $1 + $2);
                VLFS.onWriteClose(path, data);
                VLFS.registerOverlayFile(path, data);
                return 1;
            } catch(error) {
                console.error('[sqlite] managed storage publish failed', error);
                return 0;
            }
        }, path.c_str(), bytes.data(), static_cast<int>(bytes.size()));
        if(!status) throw KirikiriSqlite::StorageError(SQLITE_IOERR_FSYNC, "SQLite VLFS save-close publication failed");
    }
};

std::shared_ptr<KirikiriSqlite::Environment> environment() {
    static std::weak_ptr<KirikiriSqlite::Environment> current;
    auto value = current.lock();
    if(!value) {
        value = std::make_shared<KirikiriSqlite::Environment>(std::make_unique<TvpBacking>());
        current = value;
    }
    return value;
}

KirikiriSqlite::Value fromVariant(const tTJSVariant &value) {
    switch(value.Type()) {
        case tvtVoid: return std::monostate{};
        case tvtInteger: return static_cast<sqlite3_int64>(value.AsInteger());
        case tvtReal: return static_cast<double>(value.AsReal());
        case tvtString: return utf8(ttstr(value));
        case tvtOctet: {
            const auto *octet = value.AsOctetNoAddRef();
            return octet->GetLength() ? KirikiriSqlite::Bytes(octet->GetData(), octet->GetData() + octet->GetLength())
                                     : KirikiriSqlite::Bytes{};
        }
        case tvtObject:
            if(!value.AsObjectNoAddRef()) return std::monostate{};
            break;
    }
    TVPThrowExceptionMessage(TJS_W("SQLite parameters must be null, integer, real, string or octet"));
    return std::monostate{};
}

std::vector<KirikiriSqlite::Value> parameters(const tTJSVariant &value) {
    if(value.Type() == tvtVoid) return {};
    if(value.Type() != tvtObject || !value.AsObjectNoAddRef())
        TVPThrowExceptionMessage(TJS_W("SQLite bind requires an Array"));
    auto *array = value.AsObjectNoAddRef();
    if(array->IsInstanceOf(0, nullptr, nullptr, TJS_W("Array"), array) != TJS_S_TRUE)
        TVPThrowExceptionMessage(TJS_W("SQLite bind requires an Array"));
    tTJSVariant length;
    if(TJS_FAILED(array->PropGet(0, TJS_W("count"), nullptr, &length, array)))
        TVPThrowExceptionMessage(TJS_W("Cannot read SQLite bind Array count"));
    const auto count = length.AsInteger();
    if(count < 0 || count > 250000)
        TVPThrowExceptionMessage(TJS_W("SQLite bind Array is too large"));
    std::vector<KirikiriSqlite::Value> result;
    result.reserve(static_cast<std::size_t>(count));
    for(tjs_int index = 0; index < count; ++index) {
        tTJSVariant item;
        if(TJS_FAILED(array->PropGetByNum(0, index, &item, array)))
            TVPThrowExceptionMessage(TJS_W("Cannot read SQLite bind Array item"));
        result.push_back(fromVariant(item));
    }
    return result;
}

tTJSVariant toVariant(const KirikiriSqlite::Value &value) {
    return std::visit([](const auto &item) -> tTJSVariant {
        using T = std::decay_t<decltype(item)>;
        if constexpr(std::is_same_v<T, std::monostate>) return tTJSVariant();
        else if constexpr(std::is_same_v<T, sqlite3_int64>) return tTJSVariant(static_cast<tjs_int64>(item));
        else if constexpr(std::is_same_v<T, double>) return tTJSVariant(item);
        else if constexpr(std::is_same_v<T, std::string>) return tTJSVariant(wide(item));
        else {
            static const tjs_uint8 empty = 0;
            return tTJSVariant(item.empty() ? &empty : item.data(), static_cast<tjs_uint>(item.size()));
        }
    }, value);
}

tTJSVariant toArray(const KirikiriSqlite::Row &row) {
    auto *array = TJSCreateArrayObject();
    tTJSVariant result(array, array);
    array->Release();
    for(std::size_t index = 0; index < row.size(); ++index) {
        auto item = toVariant(row[index]);
        if(TJS_FAILED(array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(index), &item, array)))
            TVPThrowExceptionMessage(TJS_W("Cannot write SQLite result Array"));
    }
    return result;
}

} // namespace

class Sqlite {
public:
    KirikiriSqlite::Database database;
    Sqlite(const ttstr &storage, bool readonly) : database(environment(), utf8(storage), readonly) {}
    static tjs_error create(Sqlite **result, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *) {
        if(count < 1 || count > 2) return TJS_E_BADPARAMCOUNT;
        *result = new Sqlite(ttstr(*arguments[0]), count > 1 && arguments[1]->operator bool());
        return TJS_S_OK;
    }
    static tjs_error exec(tTJSVariant *result, tjs_int count, tTJSVariant **arguments, Sqlite *self) {
        if(count < 1 || count > 3) return TJS_E_BADPARAMCOUNT;
        const auto bind = count > 1 ? parameters(*arguments[1]) : std::vector<KirikiriSqlite::Value>{};
        const tTJSVariant callback = count > 2 ? *arguments[2] : tTJSVariant();
        const bool callable = callback.Type() == tvtObject && callback.AsObjectNoAddRef();
        if(count > 2 && callback.Type() != tvtVoid && !callable) return TJS_E_INVALIDPARAM;
        const bool success = self->database.exec(utf8(ttstr(*arguments[0])), bind, [&](const KirikiriSqlite::Row &row) {
            if(!callable) return;
            std::vector<tTJSVariant> values;
            std::vector<tTJSVariant *> pointers;
            values.reserve(row.size()); pointers.reserve(row.size());
            for(const auto &value : row) values.push_back(toVariant(value));
            for(auto &value : values) pointers.push_back(&value);
            const auto closure = callback.AsObjectClosureNoAddRef();
            if(TJS_FAILED(closure.FuncCall(0, nullptr, nullptr, nullptr,
                    static_cast<tjs_int>(pointers.size()), pointers.data(), nullptr)))
                TVPThrowExceptionMessage(TJS_W("SQLite row callback failed"));
        });
        if(result) *result = success;
        return TJS_S_OK;
    }
    bool begin() { return database.exec("BEGIN"); }
    bool commit() { return database.exec("COMMIT"); }
    bool rollback() { return database.exec("ROLLBACK"); }
    tjs_int getErrorCode() const { return database.connection()->code; }
    ttstr getErrorMessage() const { return wide(database.connection()->message); }
    tjs_int64 getLastInsertRowId() const { return database.lastInsertRowId(); }
};

class SqliteStatement {
    KirikiriSqlite::Statement statement_;
public:
    SqliteStatement(Sqlite &database, const ttstr &sql) : statement_(database.database.connection(), utf8(sql)) {}
    static tjs_error create(SqliteStatement **result, tjs_int count, tTJSVariant **arguments, iTJSDispatch2 *) {
        if(count != 2) return TJS_E_BADPARAMCOUNT;
        if(arguments[0]->Type() != tvtObject) return TJS_E_INVALIDPARAM;
        auto *database = ncbInstanceAdaptor<Sqlite>::GetNativeInstance(arguments[0]->AsObjectNoAddRef());
        if(!database) return TJS_E_INVALIDPARAM;
        *result = new SqliteStatement(*database, ttstr(*arguments[1]));
        return TJS_S_OK;
    }
    tjs_int reset() { return statement_.reset(); }
    static tjs_error bind(tTJSVariant *result, tjs_int count, tTJSVariant **arguments, SqliteStatement *self) {
        if(count != 1) return TJS_E_BADPARAMCOUNT;
        const int code = self->statement_.bind(parameters(*arguments[0]));
        if(result) *result = code;
        return TJS_S_OK;
    }
    tjs_int exec() { return statement_.execute(); }
    bool step() { return statement_.execute() == SQLITE_ROW; }
    static tjs_error get(tTJSVariant *result, tjs_int count, tTJSVariant **arguments, SqliteStatement *self) {
        if(count > 1) return TJS_E_BADPARAMCOUNT;
        try {
            auto value = count ? toVariant(self->statement_.get(static_cast<int>(arguments[0]->AsInteger())))
                               : toArray(self->statement_.get());
            if(result) *result = value;
        } catch(const KirikiriSqlite::StorageError &error) {
            TVPThrowExceptionMessage(wide(error.what()).c_str());
        }
        return TJS_S_OK;
    }
    tjs_int finalize() { return statement_.finalize(); }
};

NCB_REGISTER_CLASS(Sqlite) {
    Factory(&Class::create);
    NCB_METHOD_RAW_CALLBACK(exec, &Class::exec, 0);
    NCB_METHOD(begin);
    NCB_METHOD(commit);
    NCB_METHOD(rollback);
    NCB_PROPERTY_RO(errorCode, getErrorCode);
    NCB_PROPERTY_RO(errorMessage, getErrorMessage);
    NCB_PROPERTY_RO(lastInsertRowId, getLastInsertRowId);
    Variant(TJS_W("SQLITE_OK"), static_cast<tjs_int>(SQLITE_OK));
    Variant(TJS_W("SQLITE_ERROR"), static_cast<tjs_int>(SQLITE_ERROR));
    Variant(TJS_W("SQLITE_BUSY"), static_cast<tjs_int>(SQLITE_BUSY));
    Variant(TJS_W("SQLITE_READONLY"), static_cast<tjs_int>(SQLITE_READONLY));
    Variant(TJS_W("SQLITE_ROW"), static_cast<tjs_int>(SQLITE_ROW));
    Variant(TJS_W("SQLITE_DONE"), static_cast<tjs_int>(SQLITE_DONE));
}

NCB_REGISTER_CLASS(SqliteStatement) {
    Factory(&Class::create);
    NCB_METHOD(reset);
    NCB_METHOD_RAW_CALLBACK(bind, &Class::bind, 0);
    NCB_METHOD(exec);
    NCB_METHOD(step);
    NCB_METHOD_RAW_CALLBACK(get, &Class::get, 0);
    NCB_METHOD(finalize);
}
