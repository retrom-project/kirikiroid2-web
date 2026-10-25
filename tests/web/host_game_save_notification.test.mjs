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
  `(function(phase, id, success) ${source.slice(bodyStart, bodyEnd)}\n})`,
  {Module, console: {warn: (...args) => warnings.push(args)}});

notify(0, 1, 0); // An absent callback cannot affect the original TJS call.
Module.onGameSave = detail => notifications.push(JSON.parse(JSON.stringify(detail)));
notify(0, 1, 0);
notify(1, 1, 1);
notify(0, 2, 0);
notify(1, 2, 0);
assert.deepEqual(notifications, [
  {phase: 'begin', id: 1}, {phase: 'end', id: 1, success: true},
  {phase: 'begin', id: 2}, {phase: 'end', id: 2, success: false},
]);
for (const event of notifications) assert.ok(Number.isSafeInteger(event.id) && event.id > 0);
Module.onGameSave = () => { throw new Error('Host rejected a notification'); };
assert.doesNotThrow(() => notify(0, 3, 0));
assert.doesNotThrow(() => notify(1, 3, 1));
assert.equal(warnings.length, 2);
assert.match(warnings[0][0], /Host notification failed/);
console.log('Production native-save notifications preserve phase/id/boolean and isolate Host exceptions');
