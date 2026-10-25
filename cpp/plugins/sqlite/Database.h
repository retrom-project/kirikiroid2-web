#pragma once

#include "ManagedStorage.h"
#include <atomic>
#include <functional>
#include <variant>

namespace KirikiriSqlite {

using Value = std::variant<std::monostate, sqlite3_int64, double, std::string, Bytes>;
using Row = std::vector<Value>;

struct Environment {
    static std::string nextVfsName() {
        static std::atomic<unsigned int> sequence{0};
        return "kirikiri-storage-" + std::to_string(++sequence);
    }
    std::unique_ptr<Backing> backing;
    ManagedStorage storage;
    StorageVfs vfs;
    explicit Environment(std::unique_ptr<Backing> backing)
        : backing(std::move(backing)), storage(*this->backing), vfs(storage, nextVfsName()) {}
};

struct Connection {
    std::shared_ptr<Environment> environment;
    sqlite3 *handle = nullptr;
    bool valid = true;
    int code = SQLITE_OK;
    std::string message;
    explicit Connection(std::shared_ptr<Environment> environment)
        : environment(std::move(environment)) {}
    ~Connection() { close(); }
    int report(int result) {
        code = result;
        const auto storageError = environment->vfs.takeLastError();
        message = result == SQLITE_OK || result == SQLITE_ROW || result == SQLITE_DONE
            ? "" : handle ? sqlite3_errmsg(handle) : sqlite3_errstr(result);
        if(!message.empty() && !storageError.empty()) message += ": " + storageError;
        return result;
    }
    int fail(int result, const std::string &reason) { code = result; message = reason; return result; }
    bool usable() {
        if(valid && handle) return true;
        fail(SQLITE_MISUSE, "SQLite connection is closed"); return false;
    }
    int flushAfter(int result) {
        if((result == SQLITE_DONE || result == SQLITE_OK) && handle && sqlite3_get_autocommit(handle)) {
            try { environment->storage.flush(); }
            catch(const StorageError &error) { return fail(error.code, error.what()); }
            catch(const std::exception &error) { return fail(SQLITE_IOERR_FSYNC, error.what()); }
        }
        return report(result);
    }
    void close() {
        valid = false;
        if(handle) {
            // Statements may outlive the script database object. SQLite owns
            // their deferred connection lifetime; they cannot execute again.
            sqlite3_close_v2(handle); handle = nullptr;
        }
    }
};

class Statement {
    std::shared_ptr<Connection> connection_;
    sqlite3_stmt *statement_ = nullptr;
    bool row_ = false;
public:
    Statement(std::shared_ptr<Connection> connection, const std::string &sql,
              const char **tail = nullptr) : connection_(std::move(connection)) {
        if(!connection_->usable()) return;
        connection_->report(sqlite3_prepare_v2(connection_->handle, sql.c_str(),
            static_cast<int>(sql.size()), &statement_, tail));
    }
    ~Statement() { if(statement_) sqlite3_finalize(statement_); }
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    bool usable() {
        if(!connection_->usable()) return false;
        if(statement_) return true;
        if(connection_->code == SQLITE_OK) connection_->fail(SQLITE_MISUSE, "SQLite statement is empty or finalized");
        return false;
    }
    bool empty() const { return statement_ == nullptr; }
    int reset() {
        if(!usable()) return connection_->code;
        row_ = false;
        return connection_->report(sqlite3_reset(statement_));
    }
    int bind(const std::vector<Value> &parameters) {
        if(!usable()) return connection_->code;
        int code = sqlite3_clear_bindings(statement_);
        if(code != SQLITE_OK) return connection_->report(code);
        int index = 0;
        for(const auto &value : parameters) {
            ++index;
            code = std::visit([&](const auto &item) -> int {
                using T = std::decay_t<decltype(item)>;
                if constexpr(std::is_same_v<T, std::monostate>) return sqlite3_bind_null(statement_, index);
                else if constexpr(std::is_same_v<T, sqlite3_int64>) return sqlite3_bind_int64(statement_, index, item);
                else if constexpr(std::is_same_v<T, double>) return sqlite3_bind_double(statement_, index, item);
                else if constexpr(std::is_same_v<T, std::string>)
                    return sqlite3_bind_text(statement_, index, item.data(), static_cast<int>(item.size()), SQLITE_TRANSIENT);
                else {
                    // A zero-length BLOB remains BLOB, rather than becoming
                    // SQL NULL through a null vector data pointer.
                    static const unsigned char empty = 0;
                    return sqlite3_bind_blob(statement_, index, item.empty() ? &empty : item.data(),
                        static_cast<int>(item.size()), SQLITE_TRANSIENT);
                }
            }, value);
            if(code != SQLITE_OK) break;
        }
        return connection_->report(code);
    }
    int execute() {
        if(!usable()) return connection_->code;
        const int code = sqlite3_step(statement_);
        row_ = code == SQLITE_ROW;
        return connection_->flushAfter(code);
    }
    bool hasRow() const { return row_; }
    int columnCount() const { return statement_ ? sqlite3_column_count(statement_) : 0; }
    Value get(int index) {
        if(!usable()) throw StorageError(connection_->code, connection_->message);
        if(!row_) throw StorageError(SQLITE_MISUSE, "SQLite statement has no current row");
        if(index < 0 || index >= columnCount()) throw StorageError(SQLITE_RANGE, "SQLite column index is out of range");
        switch(sqlite3_column_type(statement_, index)) {
            case SQLITE_NULL: return std::monostate{};
            case SQLITE_INTEGER: return sqlite3_column_int64(statement_, index);
            case SQLITE_FLOAT: return sqlite3_column_double(statement_, index);
            case SQLITE_TEXT: {
                const auto *data = reinterpret_cast<const char *>(sqlite3_column_text(statement_, index));
                return std::string(data ? data : "", sqlite3_column_bytes(statement_, index));
            }
            case SQLITE_BLOB: {
                const int length = sqlite3_column_bytes(statement_, index);
                const auto *data = static_cast<const unsigned char *>(sqlite3_column_blob(statement_, index));
                return length ? Bytes(data, data + length) : Bytes{};
            }
        }
        throw StorageError(SQLITE_MISMATCH, "Unknown SQLite column type");
    }
    Row get() {
        Row row;
        for(int index = 0; index < columnCount(); ++index) row.push_back(get(index));
        return row;
    }
    int finalize() {
        row_ = false;
        const int code = statement_ ? sqlite3_finalize(statement_) : SQLITE_OK;
        statement_ = nullptr;
        return connection_->report(code);
    }
};

class Database {
    std::shared_ptr<Connection> connection_;
public:
    Database(std::shared_ptr<Environment> environment, const std::string &storage, bool readonly)
        : connection_(std::make_shared<Connection>(std::move(environment))) {
        const int flags = readonly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        try {
            const auto path = connection_->environment->storage.fullPath(storage);
            connection_->report(sqlite3_open_v2(path.c_str(), &connection_->handle,
                flags | SQLITE_OPEN_FULLMUTEX, connection_->environment->vfs.name()));
        } catch(const StorageError &error) { connection_->fail(error.code, error.what()); }
        catch(const std::exception &error) { connection_->fail(SQLITE_CANTOPEN, error.what()); }
    }
    ~Database() { connection_->close(); }
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    std::shared_ptr<Connection> connection() const { return connection_; }
    bool exec(const std::string &sql, const std::vector<Value> &parameters = {},
              const std::function<void(const Row &)> &callback = {}) {
        // A row callback can invalidate its TJS database. Hold the connection
        // locally so the next step reports MISUSE without a dangling owner.
        const auto connection = connection_;
        if(!connection->usable()) return false;
        std::size_t offset = 0;
        while(offset < sql.size()) {
            const auto query = sql.substr(offset);
            const char *tail = nullptr;
            Statement statement(connection, query, &tail);
            if(connection->code != SQLITE_OK) return false;
            if(!tail || tail <= query.c_str()) return connection->fail(SQLITE_ERROR, "SQLite query made no progress"), false;
            offset += static_cast<std::size_t>(tail - query.c_str());
            if(statement.empty()) continue;
            if(statement.bind(parameters) != SQLITE_OK) return false;
            int code;
            while((code = statement.execute()) == SQLITE_ROW) if(callback) callback(statement.get());
            if(code != SQLITE_DONE) return false;
        }
        connection->report(SQLITE_OK);
        return true;
    }
    sqlite3_int64 lastInsertRowId() const {
        return connection_->valid && connection_->handle ? sqlite3_last_insert_rowid(connection_->handle) : 0;
    }
};

} // namespace KirikiriSqlite
