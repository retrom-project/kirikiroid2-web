#include <cassert>
#include <iostream>
#include "cpp/plugins/sqlite/ManagedStorage.h"

// Exercise the production TvpBacking methods against the TVP media contract.
// Full media/VM startup remains an engine acceptance check, not this fixture.
struct ttstr : std::string {
    using std::string::string;
    explicit ttstr(const std::string &value) : std::string(value) {}
    bool IsEmpty() const { return empty(); }
};
static ttstr wide(const std::string &value) { return ttstr(value); }
static std::string utf8(const ttstr &value) { return value; }
static ttstr TVPGetPlacedPath(const ttstr &value) {
    if(value == "scene.sdb") return ttstr("file://./Game/Scene.sdb");
    return ttstr();
}
static ttstr TVPNormalizeStorageName(const ttstr &value) {
    return value.find("://") == std::string::npos ? ttstr("file://./Game/" + value) : value;
}
static ttstr TVPGetLocallyAccessibleName(const ttstr &value) {
    if(value.find('>') != std::string::npos || value.rfind("file://./", 0) != 0) return ttstr();
    return ttstr("/" + value.substr(9));
}
static std::string existentQuery, streamPath, bridgePath, mirrorPath;
static KirikiriSqlite::Bytes bridgeBytes;
static bool TVPIsExistentStorageNoSearch(const ttstr &value) { existentQuery = value; return true; }
static int clears = 0, bridgeStatus = 1;
static void TVPClearStorageCaches() { ++clears; }
static int bridge(const char *path, const unsigned char *bytes, int length) {
    bridgePath = path;
    bridgeBytes = length ? KirikiriSqlite::Bytes(bytes, bytes + length) : KirikiriSqlite::Bytes{};
    return bridgeStatus;
}
static int bridge(const char *path) { bridgePath = path; return bridgeStatus; }
static int fixtureUnlink(const char *path) { mirrorPath = path; return -1; }
class TvpFile : public KirikiriSqlite::StorageFile {
public:
    explicit TvpFile(const std::string &path) { streamPath = path; }
    int read(void *, int, sqlite3_int64) override { return 0; }
    void write(const void *, int, sqlite3_int64) override {}
    void truncate(sqlite3_int64) override {}
    void sync() override {}
    sqlite3_int64 size() override { return 0; }
};
#define MAIN_THREAD_EM_ASM_INT(body, ...) bridge(__VA_ARGS__)
#define unlink fixtureUnlink
#include "sqlite-tvp-backing.h"
#undef unlink
#undef MAIN_THREAD_EM_ASM_INT

int main() {
    TvpBacking backing;
    const auto existing = backing.canonical("scene.sdb");
    assert(existing == "file://./Game/Scene.sdb");
    backing.publish(existing, {0, 255, 3});
    assert(bridgePath == "/Game/Scene.sdb" && bridgeBytes == (KirikiriSqlite::Bytes{0, 255, 3}));
    assert(clears == 1);
    const auto created = backing.canonical("new.sdb");
    assert(created == "file://./Game/new.sdb");
    backing.publish(created, {});
    assert(bridgePath == "/Game/new.sdb" && bridgeBytes.empty());
    backing.remove(existing);
    assert(bridgePath == "/Game/Scene.sdb" && mirrorPath == bridgePath && clears == 3);
    assert(backing.exists(existing) && existentQuery == existing);
    backing.read(existing);
    assert(streamPath == existing);
    const std::string archive = "file://./Game/data.xp3>scenario/scene.sdb";
    assert(backing.canonical(archive) == archive);
    assert(!backing.writable(archive));
    backing.read(archive);
    assert(streamPath == archive);
    const auto lastBridge = bridgePath;
    try { backing.publish(archive, {1}); assert(false); }
    catch(const KirikiriSqlite::StorageError &error) { assert(error.code == SQLITE_READONLY); }
    try { backing.remove(archive); assert(false); }
    catch(const KirikiriSqlite::StorageError &error) { assert(error.code == SQLITE_READONLY); }
    assert(bridgePath == lastBridge && clears == 3);
    bridgeStatus = 0;
    try { backing.publish(existing, {1}); assert(false); }
    catch(const KirikiriSqlite::StorageError &error) { assert(error.code == SQLITE_IOERR_FSYNC); }
    try { backing.remove(existing); assert(false); }
    catch(const KirikiriSqlite::StorageError &error) { assert(error.code == SQLITE_IOERR_DELETE); }
    assert(clears == 3);
    std::cout << "SQLite production TVP canonical/local path, archive stream, native bridge and error translation passed\n";
}
