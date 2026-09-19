import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';
import {webcrypto, createHash} from 'node:crypto';
const source = await fs.readFile(new URL('../../platforms/web/vlfs.js', import.meta.url), 'utf8');
const sandbox = {Blob, DataView, DecompressionStream, ReadableStream, Map, Math, TextDecoder, TextEncoder,
 Uint8Array, URL, AbortController, setTimeout, clearTimeout, console: {log(){},warn(){}}, crypto:webcrypto,
 navigator:{storage:{async getDirectory(){throw Error('unavailable');}}},window:{},
 fetch(){throw Error('VLFS must not fetch');}};
vm.createContext(sandbox); vm.runInContext(source,sandbox);
const vlfs=sandbox.window.VLFS; await vlfs.init();
assert.equal(vlfs.contentAbi,'content-io-v1');
assert.equal(vlfs.contractSha256,'9601f63ba9d1bad095b42b32a3d6167166535be246a87f0efac7c5b125ed27bf');
assert.equal(vlfs.registerRemote,undefined); assert.equal(vlfs.registerZipRemote,undefined);
assert.doesNotMatch(source,/\bfetch\s*\(|XMLHttpRequest|FULL_DOWNLOAD_FALLBACK/);
let closed=false,warm=false,reads=0,pending;
const reader={abi:'content-io-v1',id:'file-one',sizeBytes:2**32+128,
 tryReadInto(pos,out){if(closed)throw Error('CONTENT_IO_ABORTED'); if(!warm)return null;out.fill(pos%251);return out.length;},
 async readInto(pos,out,signal){if(closed||signal?.aborted)throw Error('CONTENT_IO_ABORTED');++reads;if(pending)await pending;out.fill(pos%251);warm=true;return out.length;},
 async *stream(pos,len){yield new Uint8Array(len).fill(pos%251);}};
vlfs.registerContent('/Data.xp3',{fileId:reader.id,sizeBytes:reader.sizeBytes},reader);
assert.equal(reads,0); assert.equal(vlfs.resolveCase('/data.xp3'),'/Data.xp3');
const fd=vlfs.open('/data.xp3',0);vlfs.seek(fd,2**32+3,0);
assert.equal(vlfs.readCached(fd,8),null);
assert.deepEqual(Array.from(await vlfs.read(fd,8)),Array(8).fill((2**32+3)%251));
assert.equal(vlfs.stats().blockCacheBytes,0);vlfs.seek(fd,2**32+3,0);
assert.deepEqual(Array.from(vlfs.readCached(fd,8)),Array(8).fill((2**32+3)%251));
const position=vlfs.seek(fd,0,1);let release;pending=new Promise(r=>release=r);
const active=vlfs.read(fd,8);await Promise.resolve();vlfs.close(fd);release();
await assert.rejects(active,/CONTENT_IO_ABORTED/);assert.equal(vlfs._fds.has(fd),false);
pending=null;closed=true;const eof=vlfs.open('/Data.xp3',0);vlfs.seek(eof,reader.sizeBytes,0);
assert.throws(()=>vlfs.readCached(eof,0),/CONTENT_IO_ABORTED/);
await assert.rejects(vlfs.read(eof,0),/CONTENT_IO_ABORTED/);
closed=false;const overlay=vlfs.open('/save/save.ksd',1);vlfs.write(overlay,new Uint8Array([1,2]));vlfs.close(overlay);
assert.deepEqual(Array.from(await vlfs.read(vlfs.open('/save/save.ksd',0),2)),[1,2]);
assert.equal(await vlfs.derivedKey('raw-key','dir/file',8,123),createHash('sha256').update(JSON.stringify(['vlfs-derived',1,'raw-key','dir/file',8,123])).digest('hex'));
const files=['content-io.d.ts','errors.json','protocol.schema.json','vectors.json'];
const manifest=await fs.readFile(new URL('../../platforms/web/content-io-v1/CONTRACT.sha256',import.meta.url),'utf8');
let listing='';
for(const file of files.sort()){const bytes=await fs.readFile(new URL('../../platforms/web/content-io-v1/'+file,import.meta.url));listing+=file+'\n'+createHash('sha256').update(bytes).digest('hex')+'\n';}
assert.equal(createHash('sha256').update(listing).digest('hex'),vlfs.contractSha256);assert.equal(manifest.trim(),vlfs.contractSha256);
console.log('test-vlfs: PASS');
