import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import zlib from "node:zlib";
import {webcrypto} from "node:crypto";
import {fileURLToPath} from "node:url";

const testDir = path.dirname(fileURLToPath(import.meta.url));
const vlfsSource = fs.readFileSync(
    path.resolve(testDir, "../../platforms/web/vlfs.js"), "utf8");

class MemoryFileHandle {
    constructor(name) {
        this.kind = "file";
        this.name = name;
        this.bytes = new Uint8Array(0);
    }

    async getFile() {
        return new Blob([this.bytes]);
    }

    async createWritable() {
        const handle = this;
        const chunks = [];
        const append = async value => {
            if(typeof value === "string") {
                chunks.push(new TextEncoder().encode(value));
            } else if(value instanceof Blob) {
                chunks.push(new Uint8Array(await value.arrayBuffer()));
            } else {
                chunks.push(new Uint8Array(value));
            }
        };
        const commit = () => {
            const size = chunks.reduce((sum, chunk) => sum + chunk.length, 0);
            const bytes = new Uint8Array(size);
            let offset = 0;
            for(const chunk of chunks) {
                bytes.set(chunk, offset);
                offset += chunk.length;
            }
            handle.bytes = bytes;
        };
        const stream = new WritableStream({
            write: append,
            close: commit,
        });
        stream.write = append;
        stream.close = async () => commit();
        stream.abort = async () => {};
        return stream;
    }
}

class MemoryDirectoryHandle {
    constructor(name) {
        this.kind = "directory";
        this.name = name;
        this.children = new Map();
    }

    async getDirectoryHandle(name, options = {}) {
        const current = this.children.get(name);
        if(current) {
            if(current.kind !== "directory") throw new Error(`${name} is not a directory`);
            return current;
        }
        if(!options.create) throw new Error(`missing directory ${name}`);
        const created = new MemoryDirectoryHandle(name);
        this.children.set(name, created);
        return created;
    }

    async getFileHandle(name, options = {}) {
        const current = this.children.get(name);
        if(current) {
            if(current.kind !== "file") throw new Error(`${name} is not a file`);
            return current;
        }
        if(!options.create) throw new Error(`missing file ${name}`);
        const created = new MemoryFileHandle(name);
        this.children.set(name, created);
        return created;
    }

    async removeEntry(name, options = {}) {
        const current = this.children.get(name);
        if(!current) throw new Error(`missing entry ${name}`);
        if(current.kind === "directory" && current.children.size && !options.recursive)
            throw new Error(`directory ${name} is not empty`);
        this.children.delete(name);
    }

    async *entries() {
        yield* this.children.entries();
    }
}

function crc32(bytes) {
    let crc = 0xffffffff;
    for(const byte of bytes) {
        crc ^= byte;
        for(let bit = 0; bit < 8; bit++)
            crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return (crc ^ 0xffffffff) >>> 0;
}

function makeZip(entries) {
    const localParts = [];
    const centralParts = [];
    let localOffset = 0;
    for(const spec of entries) {
        const name = Buffer.from(spec.name, "utf8");
        const plain = Buffer.from(spec.data);
        const method = spec.method;
        const compressed = method === 8 ? zlib.deflateRawSync(plain) : plain;
        const checksum = crc32(plain);

        const local = Buffer.alloc(30);
        local.writeUInt32LE(0x04034b50, 0);
        local.writeUInt16LE(20, 4);
        local.writeUInt16LE(0x0800, 6);
        local.writeUInt16LE(method, 8);
        local.writeUInt32LE(checksum, 14);
        local.writeUInt32LE(compressed.length, 18);
        local.writeUInt32LE(plain.length, 22);
        local.writeUInt16LE(name.length, 26);
        localParts.push(local, name, compressed);

        const central = Buffer.alloc(46);
        central.writeUInt32LE(0x02014b50, 0);
        central.writeUInt16LE(20, 4);
        central.writeUInt16LE(20, 6);
        central.writeUInt16LE(0x0800, 8);
        central.writeUInt16LE(method, 10);
        central.writeUInt32LE(checksum, 16);
        central.writeUInt32LE(compressed.length, 20);
        central.writeUInt32LE(plain.length, 24);
        central.writeUInt16LE(name.length, 28);
        central.writeUInt32LE(localOffset, 42);
        centralParts.push(central, name);
        localOffset += local.length + name.length + compressed.length;
    }

    const central = Buffer.concat(centralParts);
    const end = Buffer.alloc(22);
    end.writeUInt32LE(0x06054b50, 0);
    end.writeUInt16LE(entries.length, 8);
    end.writeUInt16LE(entries.length, 10);
    end.writeUInt32LE(central.length, 12);
    end.writeUInt32LE(localOffset, 16);
    return new Blob([...localParts, central, end]);
}

function loadVLFS(storageRoot) {
    const context = vm.createContext({
        Blob,
        AbortController,
        setTimeout,
        clearTimeout,
        DecompressionStream,
        Headers,
        ReadableStream,
        TextDecoder,
        TextEncoder,
        URL,
        Uint8Array,
        WritableStream,
        console,
        crypto: webcrypto,
        navigator: {storage: {getDirectory: async () => storageRoot}},
        window: {},
    });
    vm.runInContext(vlfsSource, context, {filename: "vlfs.js"});
    return context.window.VLFS;
}

async function readText(vlfs, filePath) {
    const fd = vlfs.open(filePath, 0);
    assert.notEqual(fd, -1, `cannot open ${filePath}`);
    const bytes = await vlfs.read(fd, 1024);
    vlfs.close(fd);
    return new TextDecoder().decode(bytes);
}

const storageRoot = new MemoryDirectoryHandle("root");
const resourceKey = "https://example.test/game.zip";
const etag = '"archive-v1"';
const archive = makeZip([
    {name: "game/data.xp3", data: "stored-xp3", method: 0},
    {name: "game/system/config.tjs", data: "deflated-config", method: 8},
]);

const first = loadVLFS(storageRoot);
await first.init();
const registered = await first.registerZipBlob(archive, {
    eagerDeflate: true,
    persistentCache: {resourceKey, etag},
});
assert.deepEqual(
    Array.from(registered.paths), ["/data.xp3", "/system/config.tjs"]);
assert.equal(first._entries.get("/data.xp3").kind, "fsa");
assert.equal(first._entries.get("/data.xp3").zipSource, null);
assert.equal(first._entries.get("/system/config.tjs").kind, "fsa");
assert.equal(first._entries.get("/system/config.tjs").zipSource, null);
assert.equal(await readText(first, "/data.xp3"), "stored-xp3");
assert.equal(await readText(first, "/system/config.tjs"), "deflated-config");

const opfsRoot = await storageRoot.getDirectoryHandle("vlfs-tmp");
const stateFile = await opfsRoot.getFileHandle("zip-cache-state.json");
const state = JSON.parse(await (await stateFile.getFile()).text());
assert.equal(state.version, 2);
assert.equal(state.resourceKey, resourceKey);
assert.equal(state.etag, etag);
assert.deepEqual(
    Array.from(state.entries, entry => entry.path),
    ["/data.xp3", "/system/config.tjs"]);
const cacheDir = await opfsRoot.getDirectoryHandle(state.dirName);
assert.equal(
    new TextDecoder().decode((await cacheDir.getFileHandle("data.xp3")).bytes),
    "stored-xp3");
const systemDir = await cacheDir.getDirectoryHandle("system");
assert.equal(
    new TextDecoder().decode((await systemDir.getFileHandle("config.tjs")).bytes),
    "deflated-config");
assert.equal(cacheDir.children.has("e0"), false);

// A new page restores only from OPFS: no ZIP Blob or central directory is supplied.
const second = loadVLFS(storageRoot);
await second.init();
assert.equal(await second.getZipCacheValidator(resourceKey), etag);
assert.equal(await second.getZipCacheValidator("https://example.test/other.zip"), "");
const restored = await second.restoreZipCache(resourceKey, etag);
assert.ok(restored);
assert.deepEqual(
    Array.from(restored.paths), ["/data.xp3", "/system/config.tjs"]);
assert.equal(await readText(second, "/data.xp3"), "stored-xp3");
assert.equal(await readText(second, "/system/config.tjs"), "deflated-config");
assert.equal(await second.restoreZipCache(resourceKey, '"archive-v2"'), null);
// Retrom supplies immutable ZIP bytes through Content I/O. The transport owns
// Range/ETag validation; this boundary must retain lazy stored reads and reuse
// only the derived deflate cache in a fresh instance.
const contentRoot = new MemoryDirectoryHandle("content-root");
const archiveBytes = new Uint8Array(await archive.arrayBuffer());
let streams = 0;
let reads = 0;
const reader = {
    abi: "content-io-v1", id: "zip-content", sizeBytes: archiveBytes.length,
    tryReadInto() { return null; },
    async readInto(offset, out) {
        ++reads;
        out.set(archiveBytes.subarray(offset, offset + out.length));
        return out.length;
    },
    async *stream(offset, length) {
        ++streams;
        yield archiveBytes.subarray(offset, offset + length);
    },
};
const handle = {fileId: reader.id, sizeBytes: reader.sizeBytes};
const contentFirst = loadVLFS(contentRoot);
await contentFirst.init();
await contentFirst.registerZipContent(handle, reader, "immutable-zip-v1");
assert.equal(streams, 1);
assert.equal(contentFirst._entries.get("/data.xp3").kind, "zip");
assert.equal(await readText(contentFirst, "/system/config.tjs"), "deflated-config");
const contentSecond = loadVLFS(contentRoot);
await contentSecond.init();
await contentSecond.registerZipContent(handle, reader, "immutable-zip-v1");
assert.equal(streams, 1, "fresh instance must reuse the deflate cache");
assert.equal(contentSecond._entries.get("/data.xp3").kind, "zip");
assert.equal(await readText(contentSecond, "/system/config.tjs"), "deflated-config");
const metadataReads = reads;
assert.equal(await readText(contentSecond, "/data.xp3"), "stored-xp3");
assert.ok(reads > metadataReads, "stored data must still use Content I/O");
await assert.rejects(contentSecond.registerZipContent(
    {...handle, sizeBytes: handle.sizeBytes + 1}, reader, "invalid"), /CONTENT_IO_ABI_MISMATCH/);
const changedContent = loadVLFS(contentRoot);
await changedContent.init();
await changedContent.registerZipContent(handle, reader, "immutable-zip-v2");
assert.equal(streams, 2, "a different immutable identity must not reuse stale derived bytes");

console.log("VLFS complete OPFS and Content I/O derived ZIP caches verified");
