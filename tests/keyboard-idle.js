// No hardware writes: exercise timing, failures and in-flight shutdown.
import {KeyboardIdle} from '../extension/keyboardIdle.js';
function assert(value, message) { if (!value) throw new Error(message); }
let idle = 0, brightness = 3, ready = true, writes = [], errors = [];
const controller = new KeyboardIdle({idle: () => idle, brightness: () => brightness,
    ready: () => ready, write: async value => { writes.push(value); brightness = value; },
    report: error => errors.push(error)});
await controller.tick(0);
idle = 30000;
await controller.tick(30);
await controller.tick(30);
assert(writes.join() === '0', 'Dim once at threshold');
assert(controller.saved === 3 && brightness === 0, 'Auto-off preserves selected brightness separately from hardware');
idle = 0;
await controller.tick(30);
assert(writes.join() === '0,3', 'Restore original brightness on activity');
brightness = 0;
idle = 30000;
await controller.tick(30);
assert(writes.length === 2, 'Manual off stays off');
brightness = 2;
ready = false;
await controller.tick(30);
assert(writes.length === 2, 'Read-only/offline must not write');
ready = true;
await controller.tick(30);
await controller.tick(0);
assert(writes.join() === '0,3,0,2', 'Disabling timeout restores light');
controller.write = async () => { throw new Error('offline'); };
await controller.tick(30);
await controller.tick(30);
assert(errors.length === 1, 'Failure must not cause an idle retry loop');
let complete;
controller.write = value => { writes.push(value); return new Promise(resolve => { complete = resolve; }); };
idle = 0;
await controller.tick(30);
idle = 30000;
const pending = controller.tick(30);
controller.stop();
assert(controller.busy, 'Shutdown waits for dim');
complete();
await Promise.resolve();
assert(writes.at(-1) === 2, 'Shutdown restores after pending dim');
complete();
await pending;
assert(controller.saved === null, 'Restore completes');
print('PASS: keyboard inactivity, recovery, failure and shutdown');
