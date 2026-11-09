import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
const bus = Gio.DBus.session;
const name = 'io.github.mechrevo.Control1';
const path = '/io/github/mechrevo/Control1';
for (let i = 0; i < 10; i++) {
    const result = bus.call_sync(name, path, name, 'GetStatus', null,
        new GLib.VariantType('(s)'), Gio.DBusCallFlags.NONE, 3000, null);
    const data = JSON.parse(result.deep_unpack()[0]);
    if (data.schema_version !== 1 || !Array.isArray(data.fans))
        throw new Error('Invalid status response');
}
let rejected = false;
try {
    bus.call_sync(name, path, name, 'GetStatus', new GLib.Variant('(s)', ['invalid']),
        null, Gio.DBusCallFlags.NONE, 3000, null);
} catch (e) {
    rejected = Gio.DBusError.get_remote_error(e) === 'org.freedesktop.DBus.Error.InvalidArgs';
}
if (!rejected)
    throw new Error('Unexpected arguments were not rejected');
print('PASS: 10 D-Bus snapshots and invalid argument rejection');

let denied = false;
try {
    bus.call_sync(name, path, name, 'Set', new GLib.Variant('(ss)', ['mode', 'office']),
        null, Gio.DBusCallFlags.NONE, 3000, null);
} catch (e) {
    denied = Gio.DBusError.get_remote_error(e) === 'org.freedesktop.DBus.Error.AccessDenied';
}
if (!denied)
    throw new Error('Session service must reject hardware writes');
print('PASS: session service rejects writes');
