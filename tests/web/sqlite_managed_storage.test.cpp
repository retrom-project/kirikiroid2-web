#include <cassert>
#include <iostream>
#include "cpp/plugins/sqlite/Database.h"

using namespace KirikiriSqlite;

struct Input : StorageFile {
    Bytes bytes;
    int *reads;
    Input(Bytes bytes, int &reads) : bytes(std::move(bytes)), reads(&reads) {}
    int read(void *out, int amount, sqlite3_int64 offset) override {
        ++*reads;
        if(offset >= static_cast<sqlite3_int64>(bytes.size())) return 0;
        const auto count = std::min<std::size_t>(amount, bytes.size() - offset);
        if(count) std::memcpy(out, bytes.data() + offset, count);
        return count;
    }
    void write(const void *, int, sqlite3_int64) override { throw StorageError(SQLITE_READONLY, "fixture source is immutable"); }
    void truncate(sqlite3_int64) override { throw StorageError(SQLITE_READONLY, "fixture source is immutable"); }
    void sync() override {}
    sqlite3_int64 size() override { return bytes.size(); }
};
struct SavedBacking : Backing {
    std::map<std::string, Bytes> files;
    int reads = 0, publications = 0;
    bool fail = false;
    std::string canonical(const std::string &path) override {
        return path.front() == '/' ? path : "/game/" + path;
    }
    bool writable(const std::string &path) override { return path.find('>') == std::string::npos; }
    bool exists(const std::string &path) override { return files.count(path); }
    std::unique_ptr<StorageFile> read(const std::string &path) override {
        const auto found = files.find(path);
        if(found == files.end()) throw StorageError(SQLITE_CANTOPEN, "fixture file absent");
        return std::make_unique<Input>(found->second, reads);
    }
    void publish(const std::string &path, const Bytes &bytes) override {
        if(fail) { fail = false; throw StorageError(SQLITE_IOERR_FSYNC, "managed write-close failed"); }
        files[path] = bytes; ++publications;
    }
    void remove(const std::string &path) override { files.erase(path); }
};

static sqlite3_int64 count(Database &db) {
    sqlite3_int64 count = -1;
    assert(db.exec("select count(*) from story", {}, [&](const Row &row) { count = std::get<sqlite3_int64>(row[0]); }));
    return count;
}
int main() {
    auto backing = std::make_unique<SavedBacking>();
    auto *saved = backing.get();
    auto environment = std::make_shared<Environment>(std::move(backing));
    Database db(environment, "scene.sdb", false);
    assert(db.connection()->code == SQLITE_OK);
    assert(db.exec("create table story(id integer primary key,name text,voice blob,nothing_value)"));
    Statement insert(db.connection(), "insert into story values(null,?,?,?)");
    assert(insert.bind({std::string("中文正文"), Bytes{0,255,1}, std::monostate{}}) == SQLITE_OK);
    assert(insert.execute() == SQLITE_DONE);
    assert(db.lastInsertRowId() == 1);
    assert(insert.reset() == SQLITE_OK);
    assert(insert.bind({std::string("second"), Bytes{}, sqlite3_int64(42)}) == SQLITE_OK);
    assert(insert.execute() == SQLITE_DONE);
    assert(db.exec("begin"));
    assert(db.exec("insert into story(name) values('discard')"));
    assert(db.exec("rollback"));
    assert(count(db) == 2);
    assert(db.exec("attach 'scenedata.sdb' as datadb; attach 'scenepatch.sdb' as patchdb"));
    assert(db.exec("create table datadb.text(id integer,value text); create table patchdb.text(id integer,value text)"));
    assert(db.exec("begin; insert into datadb.text values(1,'original'); insert into patchdb.text values(1,'patched'); commit"));
    Row joined;
    assert(db.exec("select patchdb.text.value from datadb.text join patchdb.text using(id)", {}, [&](const Row &row) { joined = row; }));
    assert(std::get<std::string>(joined[0]) == "patched");
    Row boundRow;
    assert(db.exec("select name from story where id=?", {sqlite3_int64(1)}, [&](const Row &row) { boundRow = row; }));
    assert(std::get<std::string>(boundRow[0]) == "中文正文");
    const auto publications = saved->publications;
    assert(db.exec("pragma synchronous=OFF; update story set name='second committed' where id=2"));
    assert(saved->publications > publications);
    assert(db.exec("pragma synchronous=FULL"));
    assert(!db.exec("invalid sql"));
    assert(db.connection()->code == SQLITE_ERROR && !db.connection()->message.empty());
    assert(insert.reset() == SQLITE_OK);
    assert(insert.bind({std::string("bad"), Bytes{}, std::monostate{}, sqlite3_int64(4)}) == SQLITE_RANGE);
    assert(!db.exec("pragma journal_mode=WAL"));
    assert(db.connection()->message.find("WAL") != std::string::npos);
    saved->fail = true;
    assert(!db.exec("insert into story(name) values('failed sync')"));
    assert((db.connection()->code & 0xff) == SQLITE_IOERR);
    assert(db.connection()->message.find("managed write-close failed") != std::string::npos);
    assert(count(db) == 2);
    // Raw native-path overlays cross the save boundary. An independent
    // environment must recover the committed typed rows from those files.
    assert(saved->files.count("/game/scene.sdb"));
    auto restoredBacking = std::make_unique<SavedBacking>();
    restoredBacking->files = saved->files;
    auto restoredEnvironment = std::make_shared<Environment>(std::move(restoredBacking));
    Database restored(restoredEnvironment, "scene.sdb", true);
    assert(restored.connection()->code == SQLITE_OK);
    assert(count(restored) == 2);
    Statement select(restored.connection(), "select name,voice,nothing_value from story where id=?");
    assert(select.bind({sqlite3_int64(1)}) == SQLITE_OK && select.execute() == SQLITE_ROW);
    auto row = select.get();
    assert(std::get<std::string>(row[0]) == "中文正文");
    assert(std::get<Bytes>(row[1]) == (Bytes{0,255,1}));
    assert(std::holds_alternative<std::monostate>(row[2]));
    assert(select.execute() == SQLITE_DONE);
    Statement emptyBlob(restored.connection(), "select voice,nothing_value from story where id=2");
    assert(emptyBlob.execute() == SQLITE_ROW);
    assert(std::get<Bytes>(emptyBlob.get(0)).empty());
    assert(std::get<sqlite3_int64>(emptyBlob.get(1)) == 42);
    assert(!restored.exec("delete from story"));
    assert(restored.connection()->code == SQLITE_READONLY);
    assert(restored.exec("attach 'scenedata.sdb' as datadb; attach 'scenepatch.sdb' as patchdb"));
    assert(restored.exec("select patchdb.text.value from datadb.text join patchdb.text using(id)", {}, [&](const Row &value) {
        assert(std::get<std::string>(value[0]) == "patched");
    }));
    // Journal deletion removes the native file; no hidden companion survives.
    const std::string journal = "/game/scene.sdb-journal";
    assert(!restoredEnvironment->storage.exists(journal));
    assert(saved->publications > 0);
    auto closedDb = std::make_unique<Database>(environment, "scene.sdb", false);
    Statement orphan(closedDb->connection(), "select count(*) from story");
    closedDb.reset();
    assert(orphan.execute() == SQLITE_MISUSE);
    assert(orphan.finalize() == SQLITE_OK);
    auto callbackOwner = std::make_unique<Database>(environment, "scene.sdb", true);
    assert(!callbackOwner->exec("select * from story", {}, [&](const Row &) { callbackOwner.reset(); }));
    // Packaged resources stay immutable and open lazily. No snapshot needs to
    // be materialized when a read-only connection accesses an original DB.
    SavedBacking packaged;
    packaged.files["/game/source.sdb"] = saved->files.at("/game/scene.sdb");
    ManagedStorage lazy(packaged);
    int flags = 0;
    auto original = lazy.open("/game/source.sdb", SQLITE_OPEN_READONLY, &flags);
    assert(packaged.reads == 0 && original->size() > 32);
    unsigned char header[32];
    assert(original->read(header, sizeof(header), 0) == sizeof(header));
    assert(packaged.reads == 1 && packaged.publications == 0);
    // SQLite itself validates raw database contents; a non-database is never
    // interpreted as a managed envelope or silently replaced by an empty DB.
    auto corrupt = std::make_unique<SavedBacking>();
    corrupt->files["/game/bad.sdb"] = Bytes(200, 0xff);
    auto bad = std::make_shared<Environment>(std::move(corrupt));
    Database invalid(bad, "bad.sdb", true);
    assert(!invalid.exec("select * from sqlite_master"));
    assert(invalid.connection()->code == SQLITE_NOTADB);
    std::cout << "Managed SQLite bind/exec/step, lifecycle, attachments and persisted reopen passed\n";
}
