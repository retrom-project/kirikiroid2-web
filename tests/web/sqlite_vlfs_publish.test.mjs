import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';

const cpp = await fs.readFile(new URL('../../cpp/plugins/sqlite/Sqlite.cpp', import.meta.url), 'utf8');
const bridge = cpp.match(/MAIN_THREAD_EM_ASM_INT\(\{([\s\S]*?)\}, path\.c_str\(\), bytes\.data\(\),/);
assert.ok(bridge, 'exercise the actual compiled SQLite publication bridge');
const publish = new Function('VLFS', 'HEAPU8', 'UTF8ToString', '$0', '$1', '$2', 'console', bridge[1]);
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
const path = '/savedata/sqlite/7363656e65.bin';
const heap = Uint8Array.of(9, 9, 1, 83, 81, 76, 0, 255, 9);
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
assert.equal(publish(vlfs, heap, () => '/savedata/sqlite/missing-hook.bin', 0, 0, 1, consoleSink), 0);
assert.equal(vlfs.has('/savedata/sqlite/missing-hook.bin'), 0);
const fresh = await freshVlfs();
for(const [name, bytes] of saved) fresh.registerOverlayFile(name, bytes);
assert.deepEqual(Array.from(fresh.readCached(fresh.open(path, 0), 6)), Array.from(original));
// Journal deletion is a nonempty envelope, captured by exactly the same
// publication path, so a fresh checkpoint does not resurrect an old journal.
const journal = path + '-journal';
vlfs.onWriteClose = (name, bytes) => {saved.set(name, bytes.slice());};
assert.equal(publish(vlfs, Uint8Array.of(0), () => journal, 0, 0, 1, consoleSink), 1);
assert.deepEqual(saved.get(journal), Uint8Array.of(0));
console.log('SQLite actual VLFS publication/copy/error/fresh-session bridge passed');
