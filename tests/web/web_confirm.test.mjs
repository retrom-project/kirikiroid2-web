// Reuse the production EM_JS-body seam used by host_game_save_notification.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';

const source = await readFile(new URL('../../cpp/core/environ/web/Platform.cpp', import.meta.url), 'utf8');
const start = source.search(/EM_(?:ASYNC_)?JS\(int, web_confirm,/u);
assert.ok(start >= 0);
const begin = source.indexOf('{', start), end = source.indexOf('\n});', begin);
assert.ok(end > begin);
const Module = {}, calls = [], nativeCalls = [];
const confirm = vm.runInNewContext(`(async function(msg, title) ${source.slice(begin, end)}\n})`, {
  Module, UTF8ToString: value => value,
  confirm(message) {nativeCalls.push(message); return false;},
});
for (const selection of [0, 1]) {
  let finish;
  Module.krkr2ShowConfirm = (message, title) => {
    calls.push({message, title});
    return new Promise(resolve => {finish = resolve;});
  };
  let completed = false;
  const waiting = confirm('Original message', 'Original caption').then(value => {completed = true; return value;});
  assert.equal(nativeCalls.length, 0, 'BROWSER_NATIVE_CONFIRM_USED');
  await Promise.resolve();
  assert.equal(completed, false, 'The native caller must wait for an actual selection');
  assert.deepEqual(calls.at(-1), {message: 'Original message', title: 'Original caption'});
  finish(selection);
  assert.equal(await waiting, selection);
}
Module.krkr2ShowConfirm = async () => {throw new Error('dialog disposed');};
await assert.rejects(confirm('Message', 'Caption'), /dialog disposed/u);
delete Module.krkr2ShowConfirm;
await assert.rejects(confirm('Message', 'Caption'), /KIRIKIRI_CONFIRM_UNAVAILABLE/u);
console.log('Web confirmation awaits custom UI, preserves both selections and propagates teardown without native dialogs');
