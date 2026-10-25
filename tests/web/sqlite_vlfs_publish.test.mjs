import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';

const cpp = await fs.readFile(new URL('../../cpp/plugins/sqlite/Sqlite.cpp', import.meta.url), 'utf8');
const bridge = cpp.match(/MAIN_THREAD_EM_ASM_INT\(\{([\s\S]*?)\}, native\.c_str\(\), bytes\.data\(\),/);
assert.ok(bridge, 'exercise the actual compiled SQLite publication bridge');
const publish = new Function('VLFS', 'HEAPU8', 'UTF8ToString', '$0', '$1', '$2', 'console', bridge[1]);
const deletion = cpp.match(/void remove\(const std::string &path\) override \{[\s\S]*?MAIN_THREAD_EM_ASM_INT\(\{([\s\S]*?)\}, native\.c_str\(\)\);/);
assert.ok(deletion, 'exercise the actual compiled SQLite unlink bridge');
const remove = new Function('VLFS', 'UTF8ToString', '$0', 'console', deletion[1]);
const source = await fs.readFile(new URL('../../platforms/web/vlfs.js', import.meta.url), 'utf8');
async function freshVlfs() {
    const sandbox = {window: {}, Uint8Array, Map, TextEncoder, TextDecoder, AbortController,
        console: {log() {}, warn() {}}, navigator: {storage: {async getDirectory() {throw Error('unavailable');}}}};
    vm.createContext(sandbox);
    vm.runInContext(source, sandbox);
    const vlfs = sandbox.window.VLFS;
    await vlfs.init();
    return vlfs;
}
const vlfs = await freshVlfs();
const path = '/game/scene.sdb';
const heap = Uint8Array.of(9, 9, 83, 81, 76, 0, 255, 16, 9);
const original = heap.slice(2, 8);
const saved = new Map();
const consoleSink = {error() {}};
vlfs.registerOverlayFile('/game/source.sdb', Uint8Array.of(83, 81, 76));
vlfs.onWriteClose = (name, bytes) => {saved.set(name, bytes.slice());};
assert.equal(publish(vlfs, heap, () => path, 0, 2, 6, consoleSink), 1);
heap.fill(0);
const fd = vlfs.open(path, 0);
assert.deepEqual(Array.from(vlfs.readCached(fd, 6)), Array.from(original));
vlfs.close(fd);
assert.deepEqual(saved.get(path), original, 'host checkpoint owns complete copied bytes');
assert.deepEqual(Array.from(vlfs.readCached(vlfs.open('/game/source.sdb', 0), 3)), [83, 81, 76]);
vlfs.onWriteClose = () => {throw Error('capture failed');};
assert.equal(publish(vlfs, Uint8Array.of(1, 7), () => path, 0, 0, 2, consoleSink), 0);
assert.deepEqual(Array.from(vlfs.readCached(vlfs.open(path, 0), 6)), Array.from(original));
vlfs.onWriteClose = null;
assert.equal(publish(vlfs, heap, () => '/game/missing-hook.sdb', 0, 0, 1, consoleSink), 0);
assert.equal(vlfs.has('/game/missing-hook.sdb'), 0);
const fresh = await freshVlfs();
for(const [name, bytes] of saved) fresh.registerOverlayFile(name, bytes);
assert.deepEqual(Array.from(fresh.readCached(fresh.open(path, 0), 6)), Array.from(original));
// CREATE publishes an actual zero-length file, not a sidecar sentinel.
vlfs.onWriteClose = (name, bytes) => {saved.set(name, bytes.slice());};
assert.equal(publish(vlfs, new Uint8Array(0), () => '/game/empty.sdb', 0, 0, 0, consoleSink), 1);
assert.equal(vlfs.has('/game/empty.sdb'), 1);
assert.equal(saved.get('/game/empty.sdb').length, 0);
// Raw journal deletion records a real tombstone. Reimporting the full overlay
// above an immutable original must hide both journal and deleted database.
const journal = path + '-journal';
assert.equal(publish(vlfs, Uint8Array.of(7, 8), () => journal, 0, 0, 2, consoleSink), 1);
assert.equal(remove(vlfs, () => journal, 0, consoleSink), 0, 'missing save deletion hook fails before unlink');
assert.equal(vlfs.has(journal), 1);
vlfs.onUnlink = name => {saved.set(name, null);};
assert.equal(remove(vlfs, () => journal, 0, consoleSink), 1);
assert.equal(vlfs.has(journal), 0);
assert.equal(saved.get(journal), null);
assert.equal(remove(vlfs, () => path, 0, consoleSink), 1);
assert.equal(saved.get(path), null);
assert.equal(remove(vlfs, () => path, 0, consoleSink), 0, 'native unlink failure is not reported as success');
const afterDelete = await freshVlfs();
afterDelete.registerOverlayFile(path, original);
afterDelete.registerOverlayFile(journal, Uint8Array.of(99));
for(const [name, bytes] of saved) {
    if(bytes === null) afterDelete.unlink(name);
    else afterDelete.registerOverlayFile(name, bytes);
}
assert.equal(afterDelete.has(path), 0);
assert.equal(afterDelete.has(journal), 0);
assert.equal(afterDelete.has('/game/empty.sdb'), 1);
console.log('SQLite actual VLFS raw publication/copy/create/error/unlink/tombstone/fresh-session bridge passed');
