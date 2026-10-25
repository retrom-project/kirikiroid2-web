#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include "cpp/plugins/sqlite/StorageVfs.h"

using namespace KirikiriSqlite;
using Bytes = std::vector<unsigned char>;
struct MemoryStorage;
struct MemoryFile : StorageFile {
    MemoryStorage &storage;
    std::string path;
    std::shared_ptr<Bytes> bytes;
    bool readonly;
    MemoryFile(MemoryStorage &, std::string, std::shared_ptr<Bytes>, bool);
    int read(void *out, int amount, sqlite3_int64 offset) override {
        const auto start = std::min<std::size_t>(offset, bytes->size());
        const auto count = std::min<std::size_t>(amount, bytes->size() - start);
        std::memcpy(out, bytes->data() + start, count);
        return count;
    }
    void write(const void *data, int amount, sqlite3_int64 offset) override {
        if(readonly) throw StorageError(SQLITE_READONLY, "readonly fixture file");
        if(offset + amount > static_cast<sqlite3_int64>(bytes->size())) bytes->resize(offset + amount);
        std::memcpy(bytes->data() + offset, data, amount);
    }
    void truncate(sqlite3_int64 size) override {
        if(readonly) throw StorageError(SQLITE_READONLY, "readonly fixture file");
        bytes->resize(size);
    }
    void sync() override;
    sqlite3_int64 size() override { return bytes->size(); }
};
struct MemoryStorage : Storage {
    std::map<std::string, std::shared_ptr<Bytes>> files;
    std::map<std::string, Bytes> persisted;
    bool failSync = false;
    std::unique_ptr<StorageFile> open(const char *name, int flags, int *outputFlags) override {
        const std::string path = name ? name : "temp";
        auto found = files.find(path);
        if(found == files.end()) {
            if(!(flags & SQLITE_OPEN_CREATE)) throw StorageError(SQLITE_CANTOPEN, "missing fixture file");
            found = files.emplace(path, std::make_shared<Bytes>()).first;
        }
        if(outputFlags) *outputFlags = flags;
        return std::make_unique<MemoryFile>(*this, path, found->second, flags & SQLITE_OPEN_READONLY);
    }
    bool exists(const std::string &name) override { return files.count(name); }
    void remove(const std::string &name) override { files.erase(name); persisted.erase(name); }
    std::string fullPath(const std::string &name) override { return name; }
    void flush() override {}
    void restore(const std::map<std::string, Bytes> &checkpoint) {
        persisted = checkpoint;
        for(const auto &[name, data] : persisted) files[name] = std::make_shared<Bytes>(data);
    }
};
MemoryFile::MemoryFile(MemoryStorage &storage, std::string path,
                       std::shared_ptr<Bytes> bytes, bool readonly)
    : storage(storage), path(std::move(path)), bytes(std::move(bytes)), readonly(readonly) {}
void MemoryFile::sync() {
    if(storage.failSync) {storage.failSync = false; throw StorageError(SQLITE_IOERR_FSYNC, "fixture flush failed");}
    storage.persisted[path] = *bytes;
}
static void exec(sqlite3 *db, const char *sql) {
    const int code = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    if(code != SQLITE_OK) std::cerr << sql << ": " << sqlite3_errmsg(db) << '\n';
    assert(code == SQLITE_OK);
}
static sqlite3_int64 scalar(sqlite3 *db, const char *sql) {
    sqlite3_stmt *statement = nullptr;
    assert(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK);
    assert(sqlite3_step(statement) == SQLITE_ROW);
    const auto value = sqlite3_column_int64(statement, 0);
    assert(sqlite3_finalize(statement) == SQLITE_OK);
    return value;
}
int main() {
    MemoryStorage storage;
    std::map<std::string, Bytes> checkpoint;
    {
        StorageVfs vfs(storage, "kiri-test-vfs");
        sqlite3 *db = nullptr;
        assert(sqlite3_open_v2("scene.sdb", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, vfs.name()) == SQLITE_OK);
        exec(db, "create table story(id integer primary key, name text, voice blob, extra)");
        sqlite3_stmt *insert = nullptr;
        assert(sqlite3_prepare_v2(db, "insert into story values(null,?,?,?)", -1, &insert, nullptr) == SQLITE_OK);
        const unsigned char blob[] = {0, 255, 1};
        assert(sqlite3_bind_text(insert, 1, "中文正文", -1, SQLITE_TRANSIENT) == SQLITE_OK);
        assert(sqlite3_bind_blob(insert, 2, blob, sizeof(blob), SQLITE_TRANSIENT) == SQLITE_OK);
        assert(sqlite3_bind_null(insert, 3) == SQLITE_OK);
        assert(sqlite3_step(insert) == SQLITE_DONE);
        assert(sqlite3_last_insert_rowid(db) == 1);
        assert(sqlite3_reset(insert) == SQLITE_OK);
        assert(sqlite3_finalize(insert) == SQLITE_OK);
        exec(db, "begin; insert into story(name) values('discard'); rollback");
        assert(scalar(db, "select count(*) from story") == 1);
        exec(db, "begin; insert into story(name) values('commit'); commit");
        assert(scalar(db, "select count(*) from story") == 2);
        exec(db, "attach 'scenedata.sdb' as datadb; attach 'scenepatch.sdb' as patchdb");
        exec(db, "create table datadb.text(scene integer, value text); create table patchdb.text(scene integer, value text)");
        exec(db, "begin; insert into datadb.text values(1,'original'); insert into patchdb.text values(1,'patched'); commit");
        assert(scalar(db, "select count(*) from datadb.text join patchdb.text using(scene)") == 1);
        sqlite3 *competing = nullptr;
        assert(sqlite3_open_v2("scene.sdb", &competing, SQLITE_OPEN_READWRITE, vfs.name()) == SQLITE_OK);
        exec(db, "begin immediate");
        assert(sqlite3_exec(competing, "begin immediate", nullptr, nullptr, nullptr) == SQLITE_BUSY);
        exec(db, "rollback");
        assert(sqlite3_close(competing) == SQLITE_OK);
        assert(sqlite3_exec(db, "not valid sql", nullptr, nullptr, nullptr) == SQLITE_ERROR);
        storage.failSync = true;
        assert(sqlite3_exec(db, "insert into story(name) values('must not commit')", nullptr, nullptr, nullptr) == SQLITE_IOERR);
        assert(scalar(db, "select count(*) from story") == 2);
        assert(sqlite3_close(db) == SQLITE_OK);
        checkpoint = storage.persisted;
        assert(checkpoint.count("scene.sdb") && checkpoint.count("scenedata.sdb") && checkpoint.count("scenepatch.sdb"));
    }
    // Only persisted bytes cross this boundary. An independent storage/VFS and
    // fresh SQLite connection must recover the committed typed data.
    MemoryStorage restored;
    restored.restore(checkpoint);
    StorageVfs vfs(restored, "kiri-restored-test-vfs");
    sqlite3 *db = nullptr;
    assert(sqlite3_open_v2("scene.sdb", &db, SQLITE_OPEN_READONLY, vfs.name()) == SQLITE_OK);
    assert(scalar(db, "select count(*) from story") == 2);
    sqlite3_stmt *row = nullptr;
    assert(sqlite3_prepare_v2(db, "select name,voice,extra from story where id=1", -1, &row, nullptr) == SQLITE_OK);
    assert(sqlite3_step(row) == SQLITE_ROW);
    assert(std::strcmp(reinterpret_cast<const char *>(sqlite3_column_text(row, 0)), "中文正文") == 0);
    assert(sqlite3_column_bytes(row, 1) == 3);
    assert(std::memcmp(sqlite3_column_blob(row, 1), "\0\xff\1", 3) == 0);
    assert(sqlite3_column_type(row, 2) == SQLITE_NULL);
    assert(sqlite3_finalize(row) == SQLITE_OK);
    assert(sqlite3_exec(db, "delete from story", nullptr, nullptr, nullptr) == SQLITE_READONLY);
    assert(sqlite3_close(db) == SQLITE_OK);
}
