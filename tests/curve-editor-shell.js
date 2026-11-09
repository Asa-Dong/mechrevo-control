import Gio from 'gi://Gio';
import Clutter from 'gi://Clutter';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as Scripting from 'resource:///org/gnome/shell/ui/scripting.js';
export const METRICS = {};
export async function run() {
    Main.overview.hide();
    const script = Gio.File.new_for_uri(import.meta.url).get_parent().get_child('curve-editor-ui.js').get_path();
    const launcher = new Gio.SubprocessLauncher({flags: Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE});
    launcher.setenv('GI_TYPELIB_PATH', '/usr/lib/gnome-shell/girepository-1.0', true);
    launcher.setenv('LD_LIBRARY_PATH', '/usr/lib/gnome-shell', true);
    launcher.setenv('GDK_BACKEND', 'wayland', true);
    launcher.setenv('GTK_A11Y', 'none', true);
    launcher.setenv('GSK_RENDERER', 'cairo', true);
    launcher.setenv('ADW_DISABLE_PORTAL', '1', true);
    const dragFile = `${GLib.getenv('MECHREVO_UI_SCREENSHOT')}.drag.json`;
    GLib.unlink(dragFile);
    launcher.setenv('MECHREVO_CURVE_DRAG', dragFile, true);
    const child = launcher.spawnv(['gjs', '-m', script]);
    await Scripting.sleep(1200);
    const stream = Gio.File.new_for_path(GLib.getenv('MECHREVO_UI_SCREENSHOT')).replace(null, false, Gio.FileCreateFlags.NONE, null);
    const actor = global.get_window_actors().find(a => a.meta_window.get_pid() === Number(child.get_identifier()));
    if (!actor) throw new Error('Curve editor window missing');
    actor.meta_window.activate(global.get_current_time());
    await Scripting.sleep(250);
    const rect = actor.meta_window.get_frame_rect();
    const [ok, bytes] = GLib.file_get_contents(dragFile);
    if (!ok) throw new Error('Pointer drag fixture missing');
    const point = JSON.parse(new TextDecoder().decode(bytes));
    const pointer = Clutter.get_default_backend().get_default_seat().create_virtual_device(Clutter.InputDeviceType.POINTER_DEVICE);
    // The first click can be consumed while the new Wayland window gains pointer focus.
    for (let attempt = 0; attempt < 2; attempt++) {
        pointer.notify_absolute_motion(GLib.get_monotonic_time(), rect.x + point.x, rect.y + point.y);
        await Scripting.sleep(150);
        pointer.notify_button(GLib.get_monotonic_time(), 1, Clutter.ButtonState.PRESSED);
        await Scripting.sleep(150);
        pointer.notify_absolute_motion(GLib.get_monotonic_time(), rect.x + point.x, rect.y + point.endY);
        await Scripting.sleep(150);
        pointer.notify_button(GLib.get_monotonic_time(), 1, Clutter.ButtonState.RELEASED);
        await Scripting.sleep(150);
    }
    GLib.unlink(dragFile);
    await new Shell.Screenshot().screenshot_area(rect.x, rect.y, rect.width, rect.height, stream);
    stream.close(null);
    const output = await new Promise((resolve, reject) => child.communicate_utf8_async(null, null, (p, r) => {
        try { resolve(p.communicate_utf8_finish(r)); } catch (e) { reject(e); }
    }));
    print(output[1]);
    if (!child.get_successful()) throw new Error(output[2]);
}
