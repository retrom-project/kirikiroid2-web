#pragma once

#include <sqlite3.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace KirikiriSqlite {

class StorageError : public std::runtime_error {
public:
    int code;
    StorageError(int code, const std::string &message)
        : std::runtime_error(message), code(code) {}
};

struct StorageFile {
    virtual ~StorageFile() = default;
    virtual int read(void *, int, sqlite3_int64) = 0;
    virtual void write(const void *, int, sqlite3_int64) = 0;
    virtual void truncate(sqlite3_int64) = 0;
    virtual void sync() = 0;
    virtual sqlite3_int64 size() = 0;
};

struct Storage {
    virtual ~Storage() = default;
    virtual std::unique_ptr<StorageFile> open(const char *, int, int *) = 0;
    virtual bool exists(const std::string &) = 0;
    virtual void remove(const std::string &) = 0;
    virtual std::string fullPath(const std::string &) = 0;
    virtual void flush() = 0;
};

// SQLite's real pager, query engine and transaction journal run unchanged.
// Only their file operations cross the portable Kiri storage boundary.
class StorageVfs {
    struct State {
        StorageVfs *vfs;
        std::unique_ptr<StorageFile> file;
        std::string path;
        int flags;
    };
    struct File { sqlite3_file base; State *state; };
    sqlite3_vfs api_{};
    sqlite3_vfs *platform_;
    Storage &storage_;
    std::string name_, lastError_;
    std::mutex mutex_;
    std::map<std::string, std::map<File *, int>> locks_;

    static StorageVfs &owner(sqlite3_vfs *api) {
        return *static_cast<StorageVfs *>(api->pAppData);
    }
    static File &file(sqlite3_file *base) { return *reinterpret_cast<File *>(base); }

    template<class Operation>
    static int run(StorageVfs &vfs, int failure, Operation operation) noexcept {
        try { return operation(); }
        catch(const StorageError &error) { vfs.lastError_ = error.what(); return error.code; }
        catch(const std::exception &error) { vfs.lastError_ = error.what(); return failure; }
        catch(...) { vfs.lastError_ = "Kiri SQLite storage operation failed"; return failure; }
    }

    static int open(sqlite3_vfs *api, const char *path, sqlite3_file *base,
                    int flags, int *outputFlags) {
        auto &vfs = owner(api);
        auto &entry = file(base);
        entry.base.pMethods = nullptr;
        entry.state = nullptr;
        return run(vfs, SQLITE_CANTOPEN, [&]() {
            auto opened = vfs.storage_.open(path, flags, outputFlags);
            if(!opened) return SQLITE_CANTOPEN;
            entry.state = new State{&vfs, std::move(opened), path ? path : "", flags};
            entry.base.pMethods = &methods();
            return SQLITE_OK;
        });
    }

    static int close(sqlite3_file *base) {
        auto &entry = file(base);
        std::unique_ptr<State> state(entry.state);
        entry.state = nullptr;
        entry.base.pMethods = nullptr;
        return run(*state->vfs, SQLITE_IOERR_CLOSE, [&]() {
            {
                std::lock_guard<std::mutex> guard(state->vfs->mutex_);
                auto found = state->vfs->locks_.find(state->path);
                if(found != state->vfs->locks_.end()) {
                    found->second.erase(&entry);
                    if(found->second.empty()) state->vfs->locks_.erase(found);
                }
            }
            state->file.reset();
            if((state->flags & SQLITE_OPEN_DELETEONCLOSE) && !state->path.empty())
                state->vfs->storage_.remove(state->path);
            return SQLITE_OK;
        });
    }

    static int read(sqlite3_file *base, void *data, int amount, sqlite3_int64 offset) {
        auto &state = *file(base).state;
        return run(*state.vfs, SQLITE_IOERR_READ, [&]() {
            if(amount < 0 || offset < 0) return SQLITE_IOERR_READ;
            const int count = state.file->read(data, amount, offset);
            if(count < 0 || count > amount) return SQLITE_IOERR_READ;
            if(count == amount) return SQLITE_OK;
            std::memset(static_cast<char *>(data) + count, 0, amount - count);
            return SQLITE_IOERR_SHORT_READ;
        });
    }

    static int write(sqlite3_file *base, const void *data, int amount, sqlite3_int64 offset) {
        auto &state = *file(base).state;
        return run(*state.vfs, SQLITE_IOERR_WRITE, [&]() {
            if(amount < 0 || offset < 0) return SQLITE_IOERR_WRITE;
            state.file->write(data, amount, offset);
            return SQLITE_OK;
        });
    }

    static int truncate(sqlite3_file *base, sqlite3_int64 size) {
        auto &state = *file(base).state;
        return run(*state.vfs, SQLITE_IOERR_TRUNCATE, [&]() {
            if(size < 0) return SQLITE_IOERR_TRUNCATE;
            state.file->truncate(size); return SQLITE_OK;
        });
    }
    static int sync(sqlite3_file *base, int) {
        auto &state = *file(base).state;
        return run(*state.vfs, SQLITE_IOERR_FSYNC, [&]() { state.file->sync(); return SQLITE_OK; });
    }
    static int size(sqlite3_file *base, sqlite3_int64 *result) {
        auto &state = *file(base).state;
        return run(*state.vfs, SQLITE_IOERR_FSTAT, [&]() { *result = state.file->size(); return SQLITE_OK; });
    }

    static int lock(sqlite3_file *base, int requested) {
        auto &entry = file(base);
        auto &vfs = *entry.state->vfs;
        std::lock_guard<std::mutex> guard(vfs.mutex_);
        auto &owners = vfs.locks_[entry.state->path];
        if(owners[&entry] >= requested) return SQLITE_OK;
        for(const auto &[other, level] : owners) {
            if(other == &entry) continue;
            if((requested == SQLITE_LOCK_SHARED && level >= SQLITE_LOCK_PENDING) ||
               (requested >= SQLITE_LOCK_RESERVED && level >= SQLITE_LOCK_RESERVED) ||
               (requested == SQLITE_LOCK_EXCLUSIVE && level >= SQLITE_LOCK_SHARED))
                return SQLITE_BUSY;
        }
        owners[&entry] = requested;
        return SQLITE_OK;
    }
    static int unlock(sqlite3_file *base, int requested) {
        auto &entry = file(base);
        auto &vfs = *entry.state->vfs;
        std::lock_guard<std::mutex> guard(vfs.mutex_);
        auto &level = vfs.locks_[entry.state->path][&entry];
        level = std::min(level, requested);
        return SQLITE_OK;
    }
    static int reserved(sqlite3_file *base, int *result) {
        auto &entry = file(base);
        auto &vfs = *entry.state->vfs;
        std::lock_guard<std::mutex> guard(vfs.mutex_);
        *result = 0;
        for(const auto &[other, level] : vfs.locks_[entry.state->path]) {
            (void)other;
            if(level >= SQLITE_LOCK_RESERVED) *result = 1;
        }
        return SQLITE_OK;
    }
    static int control(sqlite3_file *, int operation, void *argument) {
        if(operation == SQLITE_FCNTL_PRAGMA) {
            auto **parameters = static_cast<char **>(argument);
            if(parameters[1] && parameters[2] &&
               sqlite3_stricmp(parameters[1], "journal_mode") == 0 &&
               sqlite3_stricmp(parameters[2], "wal") == 0) {
                parameters[0] = sqlite3_mprintf("WAL is not supported by Kiri SQLite storage; use rollback journals");
                return SQLITE_ERROR;
            }
        }
        return SQLITE_NOTFOUND;
    }
    static int sector(sqlite3_file *) { return 4096; }
    static int characteristics(sqlite3_file *) { return 0; }
    static const sqlite3_io_methods &methods() {
        static const sqlite3_io_methods value = {
            1, close, read, write, truncate, sync, size, lock, unlock, reserved,
            control, sector, characteristics,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        };
        return value;
    }

    static int remove(sqlite3_vfs *api, const char *path, int) {
        auto &vfs = owner(api);
        return run(vfs, SQLITE_IOERR_DELETE, [&]() { vfs.storage_.remove(path); return SQLITE_OK; });
    }
    static int access(sqlite3_vfs *api, const char *path, int, int *result) {
        auto &vfs = owner(api);
        return run(vfs, SQLITE_IOERR_ACCESS, [&]() { *result = vfs.storage_.exists(path); return SQLITE_OK; });
    }
    static int fullPath(sqlite3_vfs *api, const char *path, int size, char *result) {
        auto &vfs = owner(api);
        return run(vfs, SQLITE_CANTOPEN, [&]() {
            const auto normalized = vfs.storage_.fullPath(path);
            if(normalized.size() >= static_cast<std::size_t>(size)) return SQLITE_CANTOPEN;
            std::memcpy(result, normalized.c_str(), normalized.size() + 1);
            return SQLITE_OK;
        });
    }
    static int random(sqlite3_vfs *api, int size, char *bytes) {
        auto *platform = owner(api).platform_;
        return platform->xRandomness(platform, size, bytes);
    }
    static int sleep(sqlite3_vfs *api, int duration) {
        auto *platform = owner(api).platform_;
        return platform->xSleep(platform, duration);
    }
    static int time(sqlite3_vfs *api, double *result) {
        auto *platform = owner(api).platform_;
        return platform->xCurrentTime(platform, result);
    }
    static int lastError(sqlite3_vfs *api, int size, char *result) {
        const auto &message = owner(api).lastError_;
        if(size > 0) {
            const auto count = std::min(message.size(), static_cast<std::size_t>(size - 1));
            std::memcpy(result, message.data(), count); result[count] = 0;
        }
        return 0;
    }

public:
    StorageVfs(Storage &storage, std::string name) :
        platform_(sqlite3_vfs_find(nullptr)), storage_(storage), name_(std::move(name)) {
        if(!platform_) throw StorageError(SQLITE_CANTOPEN, "SQLite platform VFS unavailable");
        api_.iVersion = 1; api_.szOsFile = sizeof(File); api_.mxPathname = 4096;
        api_.zName = name_.c_str(); api_.pAppData = this;
        api_.xOpen = open; api_.xDelete = remove; api_.xAccess = access;
        api_.xFullPathname = fullPath; api_.xRandomness = random; api_.xSleep = sleep;
        api_.xCurrentTime = time; api_.xGetLastError = lastError;
        if(sqlite3_vfs_register(&api_, 0) != SQLITE_OK)
            throw StorageError(SQLITE_CANTOPEN, "Cannot register Kiri SQLite storage VFS");
    }
    ~StorageVfs() { sqlite3_vfs_unregister(&api_); }
    StorageVfs(const StorageVfs &) = delete;
    StorageVfs &operator=(const StorageVfs &) = delete;
    const char *name() const { return name_.c_str(); }
    std::string takeLastError() { auto result = std::move(lastError_); lastError_.clear(); return result; }
};

} // namespace KirikiriSqlite
