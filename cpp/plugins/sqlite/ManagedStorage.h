#pragma once

#include "StorageVfs.h"
#include <cstdint>
#include <vector>

namespace KirikiriSqlite {

using Bytes = std::vector<unsigned char>;

struct Backing {
    virtual ~Backing() = default;
    virtual std::string canonical(const std::string &) = 0;
    virtual bool exists(const std::string &) = 0;
    virtual std::unique_ptr<StorageFile> read(const std::string &) = 0;
    // Publish a complete replacement, including the write-close notification.
    // Failure must throw: the SQLite pager must not report a durable commit.
    virtual void publish(const std::string &, const Bytes &) = 0;
};

// Browser boundary: writable databases and journals are bounded small-file
// overlays under /savedata. Packaged read-only databases remain lazy streams.
// A one-byte envelope records deletion as well as content because the host's
// existing bookmark format contains files but has no file-deletion operation.
class ManagedStorage : public Storage {
    static constexpr std::size_t MaximumWritableBytes = 64 * 1024 * 1024;
    struct Entry {
        std::string path;
        std::unique_ptr<StorageFile> source;
        sqlite3_int64 sourceOffset = 0;
        sqlite3_int64 sourceSize = 0;
        Bytes bytes;
        bool loaded = false, deleted = false, dirty = false, temporary = false;
    };
    struct File : StorageFile {
        ManagedStorage &storage;
        std::shared_ptr<Entry> entry;
        bool readonly;
        File(ManagedStorage &storage, std::shared_ptr<Entry> entry, bool readonly)
            : storage(storage), entry(std::move(entry)), readonly(readonly) {}
        int read(void *out, int amount, sqlite3_int64 offset) override {
            if(entry->deleted) throw StorageError(SQLITE_IOERR_READ, "SQLite file was deleted");
            const auto length = size();
            if(offset >= length) return 0;
            const int count = static_cast<int>(std::min<sqlite3_int64>(amount, length - offset));
            if(entry->loaded) {
                if(count) std::memcpy(out, entry->bytes.data() + offset, count);
                return count;
            }
            return entry->source->read(out, count, entry->sourceOffset + offset);
        }
        void writable() {
            if(readonly) throw StorageError(SQLITE_READONLY, "SQLite storage is read-only");
            if(entry->deleted) throw StorageError(SQLITE_IOERR_WRITE, "SQLite file was deleted");
            storage.materialize(*entry);
        }
        void write(const void *data, int amount, sqlite3_int64 offset) override {
            writable();
            if(offset > static_cast<sqlite3_int64>(MaximumWritableBytes) - amount)
                throw StorageError(SQLITE_FULL, "SQLite writable storage exceeds 64 MiB");
            const auto end = static_cast<std::size_t>(offset + amount);
            if(end > entry->bytes.size()) entry->bytes.resize(end);
            if(amount) std::memcpy(entry->bytes.data() + offset, data, amount);
            entry->dirty = true;
        }
        void truncate(sqlite3_int64 length) override {
            writable();
            if(length > static_cast<sqlite3_int64>(MaximumWritableBytes))
                throw StorageError(SQLITE_FULL, "SQLite writable storage exceeds 64 MiB");
            entry->bytes.resize(static_cast<std::size_t>(length));
            entry->dirty = true;
        }
        void sync() override { if(!readonly) storage.persist(*entry); }
        sqlite3_int64 size() override {
            return entry->loaded ? static_cast<sqlite3_int64>(entry->bytes.size()) : entry->sourceSize;
        }
    };
    Backing &backing_;
    std::map<std::string, std::shared_ptr<Entry>> entries_;
    std::uint64_t nextTemporary_ = 0;

    void materialize(Entry &entry) {
        if(entry.loaded) return;
        if(entry.sourceSize > static_cast<sqlite3_int64>(MaximumWritableBytes))
            throw StorageError(SQLITE_FULL, "SQLite writable storage exceeds 64 MiB");
        Bytes bytes(static_cast<std::size_t>(entry.sourceSize));
        sqlite3_int64 offset = 0;
        while(offset < entry.sourceSize) {
            const int amount = static_cast<int>(std::min<sqlite3_int64>(64 * 1024, entry.sourceSize - offset));
            const int count = entry.source->read(bytes.data() + offset, amount, entry.sourceOffset + offset);
            if(count <= 0 || count > amount)
                throw StorageError(SQLITE_IOERR_READ, "SQLite source ended before its declared size");
            offset += count;
        }
        entry.bytes = std::move(bytes);
        entry.source.reset(); entry.loaded = true;
    }
    void persist(Entry &entry) {
        if(!entry.dirty) return;
        if(!entry.temporary) {
            Bytes snapshot;
            snapshot.reserve(entry.bytes.size() + 1);
            snapshot.push_back(entry.deleted ? 0 : 1);
            if(!entry.deleted) snapshot.insert(snapshot.end(), entry.bytes.begin(), entry.bytes.end());
            backing_.publish(snapshotPath(entry.path), snapshot);
        }
        entry.dirty = false;
    }
    std::shared_ptr<Entry> lookup(const std::string &path) {
        auto found = entries_.find(path);
        if(found != entries_.end()) return found->second;
        auto entry = std::make_shared<Entry>(); entry->path = path;
        const auto snapshot = snapshotPath(path);
        if(backing_.exists(snapshot)) {
            entry->source = backing_.read(snapshot);
            const auto length = entry->source->size();
            unsigned char tag = 0xff;
            if(length < 1 || entry->source->read(&tag, 1, 0) != 1 || tag > 1 || (tag == 0 && length != 1))
                throw StorageError(SQLITE_CORRUPT, "Invalid managed SQLite storage envelope");
            entry->deleted = tag == 0;
            entry->sourceOffset = 1; entry->sourceSize = length - 1;
        } else if(backing_.exists(path)) {
            entry->source = backing_.read(path);
            entry->sourceSize = entry->source->size();
        } else {
            entry->deleted = true;
        }
        entries_[path] = entry;
        return entry;
    }

public:
    explicit ManagedStorage(Backing &backing) : backing_(backing) {}
    static std::string snapshotPath(const std::string &canonical) {
        static constexpr char hex[] = "0123456789abcdef";
        std::string path = "/savedata/sqlite/";
        for(unsigned char byte : canonical) {
            path += hex[byte >> 4]; path += hex[byte & 15];
        }
        return path + ".bin";
    }
    std::unique_ptr<StorageFile> open(const char *name, int flags, int *outputFlags) override {
        std::shared_ptr<Entry> entry;
        if(!name) {
            entry = std::make_shared<Entry>();
            entry->path = "temporary-" + std::to_string(++nextTemporary_);
            entry->loaded = entry->temporary = true;
        } else {
            entry = lookup(name);
            if(entry->deleted) {
                if(!(flags & SQLITE_OPEN_CREATE)) throw StorageError(SQLITE_CANTOPEN, "SQLite storage does not exist");
                entry->deleted = false; entry->loaded = true; entry->dirty = true;
                entry->source.reset(); entry->sourceSize = 0; entry->bytes.clear();
            } else if((flags & SQLITE_OPEN_CREATE) && (flags & SQLITE_OPEN_EXCLUSIVE)) {
                throw StorageError(SQLITE_CANTOPEN, "SQLite exclusive storage already exists");
            }
        }
        if(outputFlags) *outputFlags = flags;
        return std::make_unique<File>(*this, std::move(entry), flags & SQLITE_OPEN_READONLY);
    }
    bool exists(const std::string &path) override { return !lookup(path)->deleted; }
    void remove(const std::string &path) override {
        auto entry = lookup(path);
        if(entry->deleted) return;
        entry->bytes.clear(); entry->source.reset(); entry->sourceSize = 0;
        entry->loaded = entry->deleted = entry->dirty = true;
        persist(*entry);
    }
    std::string fullPath(const std::string &path) override { return backing_.canonical(path); }
    void flush() override {
        for(auto &[path, entry] : entries_) { (void)path; persist(*entry); }
    }
};

} // namespace KirikiriSqlite
