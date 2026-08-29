import assert from "node:assert/strict";
import fs from "node:fs/promises";
import vm from "node:vm";
import { webcrypto } from "node:crypto";

const source = await fs.readFile(new URL("../../platforms/web/vlfs.js", import.meta.url), "utf8");
const sandbox = {
    Blob,
    DataView,
    DecompressionStream,
    Map,
    Math,
    TextDecoder,
    Uint8Array,
    URL,
    console: { log() {}, warn() {} },
    crypto: webcrypto,
    navigator: { storage: { async getDirectory() { throw new Error("not used"); } } },
    window: {},
};
vm.createContext(sandbox);
vm.runInContext(source, sandbox, { filename: "vlfs.js" });
const vlfs = sandbox.window.VLFS;
await vlfs.init();

function headers(values = {}) {
    const normalized = new Map(Object.entries(values).map(([key, value]) => [key.toLowerCase(), value]));
    return { get(name) { return normalized.get(name.toLowerCase()) ?? null; } };
}

function response(status, body, headerValues = {}) {
    return {
        status,
        headers: headers(headerValues),
        body: { cancel() {} },
        async arrayBuffer() { return Uint8Array.from(body).buffer; },
    };
}

let requestedRange = "";
sandbox.fetch = async (_url, options) => {
    requestedRange = options.headers.Range;
    return response(206, [4, 5, 6, 7], {
        "content-length": "4",
        "content-range": "bytes 4-7/8388608",
    });
};
vlfs.registerRemote("/strict.bin", "https://content.example/strict.bin", 8 * 1024 * 1024, true);
assert.deepEqual(Array.from(await vlfs._readSource(vlfs._entries.get("/strict.bin"), 4, 4)), [4, 5, 6, 7]);
assert.equal(requestedRange, "bytes=4-7");

sandbox.fetch = async () => response(206, [4, 5, 6, 7], {
    "content-length": "4",
    "content-range": "bytes 1-4/8388608",
});
vlfs.registerRemote("/wrong-range.bin", "https://content.example/wrong-range.bin", 8 * 1024 * 1024, true);
await assert.rejects(
    () => vlfs._readSource(vlfs._entries.get("/wrong-range.bin"), 0, 4),
    /range response mismatch/,
);

let largeBodyRead = false;
sandbox.fetch = async () => ({
    status: 200,
    headers: headers({ "content-length": String(8 * 1024 * 1024) }),
    body: { cancel() {} },
    async arrayBuffer() {
        largeBodyRead = true;
        return new ArrayBuffer(8 * 1024 * 1024);
    },
});
vlfs.registerRemote("/large.bin", "https://content.example/large.bin", 8 * 1024 * 1024, true);
await assert.rejects(
    () => vlfs._readSource(vlfs._entries.get("/large.bin"), 0, 4),
    /range required for large remote/,
);
assert.equal(largeBodyRead, false);

sandbox.fetch = async () => response(200, [0, 1, 2, 3, 4, 5, 6, 7], { "content-length": "8" });
vlfs.registerRemote("/small.bin", "https://content.example/small.bin", 8, true);
assert.deepEqual(Array.from(await vlfs._readSource(vlfs._entries.get("/small.bin"), 3, 3)), [3, 4, 5]);

sandbox.fetch = async () => response(200, [0], { "content-length": "8388608" });
vlfs.registerRemote("/large-no-range.bin", "https://content.example/large-no-range.bin", 8 * 1024 * 1024, false);
await assert.rejects(
    () => vlfs._readSource(vlfs._entries.get("/large-no-range.bin"), 0, 1),
    /range required for large remote/,
);

console.log("test-vlfs: PASS");
