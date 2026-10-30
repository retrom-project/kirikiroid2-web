// Run the production EM_JS notification body, without copying its behavior.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';

const source = await readFile(new URL('../../cpp/core/environ/web/HostBookmarkBridge.cpp', import.meta.url), 'utf8');
const start = source.indexOf('EM_JS(void, krkr2_host_notify_game_save,');
assert.ok(start >= 0, 'production native-save notification must exist');
const bodyStart = source.indexOf('{', start);
const bodyEnd = source.indexOf('\n});', bodyStart);
assert.ok(bodyEnd > bodyStart);
const Module = {}, notifications = [], warnings = [];
const notify = vm.runInNewContext(
  `(function(phase, id, success, history) ${source.slice(bodyStart, bodyEnd)}\n})`,
  {Module, console: {warn: (...args) => warnings.push(args)}});

notify(0, 1, 0, 1); // An absent callback cannot affect the original TJS call.
Module.onGameSave = detail => notifications.push(JSON.parse(JSON.stringify(detail)));
notify(0, 1, 0, 1);
notify(1, 1, 1, 0);
notify(0, 2, 0, 0); // A system bookmark such as the KAG start anchor.
notify(1, 2, 0, 0);
assert.deepEqual(notifications, [
  {phase: 'begin', id: 1, history: true}, {phase: 'end', id: 1, success: true},
  {phase: 'begin', id: 2, history: false}, {phase: 'end', id: 2, success: false},
]);
for (const event of notifications) assert.ok(Number.isSafeInteger(event.id) && event.id > 0);
Module.onGameSave = () => { throw new Error('Host rejected a notification'); };
assert.doesNotThrow(() => notify(0, 3, 0, 1));
assert.doesNotThrow(() => notify(1, 3, 1, 0));
assert.equal(warnings.length, 2);
assert.match(warnings[0][0], /Host notification failed/);
console.log('Production native-save notifications preserve phase/id/history/boolean and isolate Host exceptions');

// The player-save image is copied out of the Wasm heap before the callback.
const imageStart = source.indexOf('EM_JS(void, krkr2_host_publish_game_save_image,');
assert.ok(imageStart >= 0, 'production native-save image notification must exist');
const imageBody = source.indexOf('{', source.indexOf(')', source.indexOf('int height)', imageStart)));
const imageEnd = source.indexOf('\n});', imageBody);
const HEAPU8 = new Uint8Array(16);
HEAPU8.set([1, 2, 3, 255, 4, 5, 6, 255], 4);
const images = [], imageWarnings = [];
const imageModule = {onGameSaveImage: detail => images.push(detail)};
const publish = vm.runInNewContext(
  `(function(id, pixels, width, height) ${source.slice(imageBody, imageEnd)}\n})`,
  {Module: imageModule, HEAPU8, console: {warn: (...args) => imageWarnings.push(args)}});
publish(7, 4, 2, 1);
HEAPU8.fill(0);
assert.equal(images.length, 1);
assert.deepEqual({id: images[0].id, width: images[0].width, height: images[0].height}, {id: 7, width: 2, height: 1});
assert.deepEqual([...images[0].pixels], [1, 2, 3, 255, 4, 5, 6, 255]);
imageModule.onGameSaveImage = () => { throw new Error('Host rejected an image'); };
assert.doesNotThrow(() => publish(8, 4, 2, 1));
assert.equal(imageWarnings.length, 1);
console.log('Production native-save images are copied from the Wasm heap and isolate Host exceptions');
