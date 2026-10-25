#include <cassert>
#include <iostream>
#include "cpp/plugins/sqlite/Database.h"

using namespace KirikiriSqlite;

// Like a live VLFS descriptor, this read handle retains the opened object when
// its directory entry is removed. Closing a database must release that object.
struct NativeInput : StorageFile {
    Bytes bytes;
    explicit NativeInput(Bytes value) : bytes(std::move(value)) {}
    int read(void *out, int amount, sqlite3_int64 offset) override {
        const auto start = std::min<std::size_t>(offset, bytes.size());
        const auto count = std::min<std::size_t>(amount, bytes.size() - start);
        if(count) std::memcpy(out, bytes.data() + start, count);
        return static_cast<int>(count);
    }
    void write(const void *, int, sqlite3_int64) override { throw StorageError(SQLITE_READONLY, "immutable source"); }
    void truncate(sqlite3_int64) override { throw StorageError(SQLITE_READONLY, "immutable source"); }
    void sync() override {}
    sqlite3_int64 size() override { return bytes.size(); }
};
struct NativeBacking : Backing {
    std::map<std::string, Bytes> files;
    std::map<std::string, Bytes> writes;
    std::vector<std::string> tombstones;
    bool failPublish = false, failRemove = false;
    std::string canonical(const std::string &path) override {
        return path.front() == '/' ? path : "/game/" + path;
    }
    bool writable(const std::string &path) override { return path.find('>') == std::string::npos; }
    bool exists(const std::string &path) override { return files.count(path); }
    std::unique_ptr<StorageFile> read(const std::string &path) override {
        return std::make_unique<NativeInput>(files.at(path));
    }
    void publish(const std::string &path, const Bytes &bytes) override {
        if(failPublish) { failPublish = false; throw StorageError(SQLITE_IOERR_FSYNC, "native publication rejected"); }
        files[path] = writes[path] = bytes;
    }
    void remove(const std::string &path) override {
        if(failRemove) { failRemove = false; throw StorageError(SQLITE_IOERR_DELETE, "native deletion rejected"); }
        files.erase(path); writes.erase(path); tombstones.push_back(path);
    }
};
static sqlite3_int64 count(Database &db) {
    sqlite3_int64 result = -1;
    assert(db.exec("select count(*) from story", {}, [&](const Row &row) { result = std::get<sqlite3_int64>(row[0]); }));
    return result;
}
int main() {
    auto backing = std::make_unique<NativeBacking>();
    auto *native = backing.get();
    auto environment = std::make_shared<Environment>(std::move(backing));
    Bytes firstDatabase;
    {
        Database db(environment, "scene.sdb", false);
        assert(db.connection()->code == SQLITE_OK);
        assert(native->files.count("/game/scene.sdb") && "CREATE must be visible to native exists/getLocalName immediately");
        assert(native->files.at("/game/scene.sdb").empty());
        assert(db.exec("create table story(value integer); insert into story values(1)"));
        firstDatabase = native->files.at("/game/scene.sdb");
        assert(firstDatabase.size() > 16 && std::memcmp(firstDatabase.data(), "SQLite format 3\0", 16) == 0);
        assert(!native->files.count("/game/scene.sdb-journal"));
        assert(std::find(native->tombstones.begin(), native->tombstones.end(), "/game/scene.sdb-journal") != native->tombstones.end());
    }
    // Unrelated live databases retain the Environment, not closed file data.
    Database other(environment, "other.sdb", false);
    assert(other.exec("create table unrelated(value)"));
    native->files.erase("/game/scene.sdb"); // successful external Storages.deleteFile
    {
        Database deleted(environment, "scene.sdb", true);
        assert(deleted.connection()->code == SQLITE_CANTOPEN);
    }
    {
        Database recreated(environment, "scene.sdb", false);
        assert(recreated.exec("create table story(value integer); insert into story values(7),(8)"));
        assert(count(recreated) == 2);
    }
    auto coldBacking = std::make_unique<NativeBacking>();
    coldBacking->files = native->files;
    auto cold = std::make_shared<Environment>(std::move(coldBacking));
    Database restored(cold, "scene.sdb", true);
    assert(count(restored) == 2);
    // Restored raw bytes overlay the same original source path. No hidden
    // companion file is required to select their committed revision.
    native->files["/game/scene.sdb"] = firstDatabase;
    { Database replaced(environment, "scene.sdb", true); assert(count(replaced) == 1); }

    // A failed CREATE cannot leave a visible empty file or a cached ghost.
    native->failPublish = true;
    { Database failed(environment, "new.sdb", false); assert((failed.connection()->code & 0xff) == SQLITE_IOERR); }
    assert(!native->files.count("/game/new.sdb"));
    { Database absent(environment, "new.sdb", true); assert(absent.connection()->code == SQLITE_CANTOPEN); }
    { Database retry(environment, "new.sdb", false); assert(retry.connection()->code == SQLITE_OK); }

    // An unsuccessful delete must leave both the namespace and active Entry.
    auto opened = environment->storage.open("/game/scene.sdb", SQLITE_OPEN_READONLY, nullptr);
    native->failRemove = true;
    try { environment->storage.remove("/game/scene.sdb"); assert(false); }
    catch(const StorageError &error) { assert(error.code == SQLITE_IOERR_DELETE); }
    assert(native->files.at("/game/scene.sdb") == firstDatabase);
    unsigned char header[16];
    assert(opened->read(header, sizeof(header), 0) == sizeof(header));
    assert(std::memcmp(header, firstDatabase.data(), sizeof(header)) == 0);
    opened.reset();
    { Database retained(environment, "scene.sdb", true); assert(count(retained) == 1); }
    environment->storage.remove("/game/scene.sdb");
    assert(!native->files.count("/game/scene.sdb"));
    { Database deleted(environment, "scene.sdb", true); assert(deleted.connection()->code == SQLITE_CANTOPEN); }

    // Archive resources keep their canonical stream name and remain read-only.
    const std::string archive = "/game/data.xp3>scene.sdb";
    native->files[archive] = firstDatabase;
    const auto original = native->files.at(archive);
    { Database packaged(environment, archive, true); assert(count(packaged) == 1); }
    {
        Database requestedWritable(environment, archive, false);
        assert(requestedWritable.connection()->code == SQLITE_OK);
        assert(sqlite3_db_readonly(requestedWritable.connection()->handle, "main") == 1);
        assert(count(requestedWritable) == 1);
        assert(!requestedWritable.exec("insert into story values(9)"));
        assert(requestedWritable.connection()->code == SQLITE_READONLY);
    }
    int outputFlags = 0;
    auto readonlyFallback = environment->storage.open(archive.c_str(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, &outputFlags);
    assert((outputFlags & SQLITE_OPEN_READONLY) && !(outputFlags & (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)));
    try { readonlyFallback->write("x", 1, 0); assert(false); }
    catch(const StorageError &error) { assert(error.code == SQLITE_READONLY); }
    assert(native->files.at(archive) == original && !native->writes.count(archive));
    try { environment->storage.remove(archive); assert(false); }
    catch(const StorageError &error) { assert(error.code == SQLITE_READONLY); }
    std::cout << "SQLite native create/delete/recreate, closed-file lifetime, fresh restore, failure and archive-readonly boundaries passed\n";
}
