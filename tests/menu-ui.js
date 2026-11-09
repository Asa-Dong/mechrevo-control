// Run only in an isolated headless GNOME Shell. Hardware writes are stubbed.
import Clutter from 'gi://Clutter';
import GLib from 'gi://GLib';
import Gio from 'gi://Gio';
import Shell from 'gi://Shell';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as Scripting from 'resource:///org/gnome/shell/ui/scripting.js';
import Control from '../extension/extension.js';
export const METRICS = {};
export async function run() {
    Main.overview.hide();
    const dir = Gio.File.new_for_uri(import.meta.url).get_parent().get_parent().get_child('extension');
    const ext = new Control({uuid: 'mechrevo-ui-test', path: dir.get_path(), dir, 'settings-schema': 'org.gnome.shell.extensions.mechrevo-control'});
    ext.enable();
    try {
        await Scripting.sleep(1800);
        if (!ext._data?.vendor) throw new Error('Missing real status');
        ext._refresh = () => {}; // Keep deterministic UI fixtures after the initial real read.
        if (ext._buttons.some(e => e.setting === 'fan_control' && e.value === 'boost')) throw new Error('Full speed option still exposed');
        if (!ext._buttons.some(e => e.setting === 'fan_control' && e.value === 'curve')) throw new Error('Curve option missing');
        // Auto-off changes hardware brightness, never the user's selected slider level.
        const light = ext._sliders.find(e => e.setting === 'keyboard_brightness');
        const originalLight = ext._data.vendor.keyboard_brightness;
        ext._keyboardIdle.saved = 3;
        ext._data.vendor.keyboard_brightness = 0;
        ext._sync();
        if (light.label.text !== '3' || light.slider.value !== 0.75)
            throw new Error('Auto-off must preserve brightness 3 in the UI');
        ext._keyboardIdle.forget();
        ext._sync();
        if (light.label.text !== '0') throw new Error('Explicit off must still display 0');
        ext._data.vendor.keyboard_brightness = originalLight;
        // Display the complete layout without touching hardware fan mode.
        ext._data.vendor.fan_control = 'manual';
        ext._sync();
        ext._button.menu.open();
        await Scripting.sleep(400);
        if (ext._button.menu.actor.height > 650) throw new Error(`Menu too tall: ${ext._button.menu.actor.height}`);
        const originalFans = ext._data.fans;
        ext._data.fans = [{chip: 'mechrevo', label: 'Secondary', rpm: 2990}, {chip: 'mechrevo', label: 'Main', rpm: 2848}];
        ext._sync();
        if (!ext._summary.label.text.endsWith('2848 RPM') || !ext._gpu.label.text.endsWith('2990 RPM'))
            throw new Error('CPU/GPU RPM mapping must not depend on array order');
        ext._data.fans = [];
        ext._sync();
        if (!ext._summary.label.text.endsWith('- RPM') || !ext._gpu.label.text.endsWith('- RPM'))
            throw new Error('Missing fan readings must not become zero');
        ext._data.fans = originalFans;
        const gpu = ext._data.gpu;
        ext._data.gpu = {celsius: null, watts: null};
        ext._sync();
        if (!ext._gpu.label.text.startsWith('GPU  -  ·  -  ·  ')) throw new Error('Missing GPU values must use plain hyphens');
        for (const [state, label] of [['suspended', '已休眠'], ['transitioning', '电源切换中'], ['unavailable', '读数不可用']]) {
            ext._data.gpu = {state, celsius: null, watts: null};
            ext._sync();
            if (!ext._gpu.label.text.startsWith(`GPU  ${label}  ·  `)) throw new Error(`Wrong GPU state: ${state}`);
        }
        ext._data.gpu = gpu;
        ext._sync();
        await Scripting.sleep(100);
        const idleHeight = ext._button.menu.actor.height;
        ext._writing = true;
        ext._sync();
        await Scripting.sleep(100);
        if (ext._button.menu.actor.height !== idleHeight) throw new Error(`Busy feedback moves the menu: ${idleHeight} → ${ext._button.menu.actor.height}`);
        ext._writing = false;
        ext._sync();
        await Scripting.sleep(100);
        if (ext._button.menu.actor.height !== idleHeight) throw new Error('Idle feedback moves the menu');
        let calls = [];
        ext._set = (key, value) => calls.push([key, value]);
        for (const entry of ext._buttons) {
            if (!entry.button.reactive) throw new Error('Control is not reactive');
            entry.button.emit('clicked', 1);
            const got = calls.at(-1);
            if (got?.[0] !== entry.setting || got?.[1] !== entry.value) throw new Error('Wrong control handler');
        }
        const pointer = Clutter.get_default_backend().get_default_seat().create_virtual_device(Clutter.InputDeviceType.POINTER_DEVICE);
        const target = ext._buttons[2].button;
        const [x, y] = target.get_transformed_position();
        const [w, h] = target.get_transformed_size();
        calls = [];
        pointer.notify_absolute_motion(GLib.get_monotonic_time(), x + w / 2, y + h / 2);
        await Scripting.sleep(100);
        pointer.notify_button(GLib.get_monotonic_time(), 1, Clutter.ButtonState.PRESSED);
        pointer.notify_button(GLib.get_monotonic_time(), 1, Clutter.ButtonState.RELEASED);
        await Scripting.sleep(100);
        if (calls.at(-1)?.[1] !== 'turbo') throw new Error('Real pointer click did not reach mode button');
        if (ext._keyboardMenu.menu.isOpen) throw new Error('Keyboard submenu should start collapsed');
        const collapsedWidth = ext._button.menu.actor.width;
        ext._keyboardMenu.setSubmenuShown(true);
        await Scripting.sleep(300);
        if (ext._button.menu.actor.width !== collapsedWidth)
            throw new Error(`Keyboard submenu changes popup width: ${collapsedWidth} -> ${ext._button.menu.actor.width}`);
        const expectedSliderWidth = ext._sliders[0].slider.width;
        if (ext._sliders.some(e => e.slider.width !== expectedSliderWidth))
            throw new Error('Slider track widths differ');
        if (!ext._keyboardMenu.menu.isOpen) throw new Error('Keyboard submenu did not open');
        // Drag updates must not write until release; refresh cannot move a held slider.
        for (const e of ext._sliders) {
            calls = [];
            e.slider.emit('drag-begin');
            e.slider.value = 0.2;
            e.slider.value = 0.6;
            ext._sync();
            await Scripting.sleep(350);
            if (calls.length || Math.abs(e.slider.value - 0.6) > 0.001) throw new Error('Drag write/refresh race');
            e.slider.emit('drag-end');
            await Scripting.sleep(100);
            if (calls.length !== 1 || calls[0][0] !== e.setting) throw new Error('Slider release dispatch failed');
        }
        const timeout = ext._sliders.find(e => e.setting === 'keyboard-idle-seconds');
        const menuWidthBefore = ext._button.menu.actor.width;
        const sliderWidthBefore = timeout.slider.width;
        const sliderXBefore = timeout.slider.get_transformed_position()[0];
        timeout.slider.emit('drag-begin');
        for (const seconds of [0, 1, 9, 10, 99, 100, 300, 9, 0]) {
            timeout.slider.value = seconds / 300;
            await Scripting.sleep(60);
            if (ext._button.menu.actor.width !== menuWidthBefore ||
                timeout.slider.width !== sliderWidthBefore ||
                timeout.slider.get_transformed_position()[0] !== sliderXBefore)
                throw new Error(`Timeout ${seconds} changes menu or slider geometry`);
        }
        timeout.slider.emit('drag-end');
        await Scripting.sleep(100);
        Control.prototype._set.call(ext, 'keyboard-idle-seconds', '120');
        if (ext._settings.get_uint('keyboard-idle-seconds') !== 120 || timeout.label.text !== '120 秒')
            throw new Error('Timeout slider does not save seconds');
        Control.prototype._set.call(ext, 'keyboard-idle-seconds', '301');
        if (ext._settings.get_uint('keyboard-idle-seconds') !== 120 || timeout.maximum !== 300)
            throw new Error('Timeout upper bound must be 300 seconds');
        Control.prototype._set.call(ext, 'keyboard-idle-seconds', '0');
        if (timeout.label.text !== '关闭') throw new Error('Zero timeout must display disabled');
        // Keyboard/scroll changes are coalesced.
        calls = [];
        const bright = ext._sliders.find(e => e.setting === 'keyboard_brightness');
        bright.slider.value = 0.25;
        bright.slider.value = 0.75;
        await Scripting.sleep(400);
        if (calls.length !== 1 || calls[0][1] !== '3') throw new Error('Slider debounce failed');
        calls = [];
        const [sx, sy] = bright.slider.get_transformed_position();
        const [sw, sh] = bright.slider.get_transformed_size();
        pointer.notify_absolute_motion(GLib.get_monotonic_time(), sx + sw * 0.2, sy + sh / 2);
        await Scripting.sleep(50);
        pointer.notify_button(GLib.get_monotonic_time(), 1, Clutter.ButtonState.PRESSED);
        await Scripting.sleep(50);
        pointer.notify_absolute_motion(GLib.get_monotonic_time(), sx + sw * 0.85, sy + sh / 2);
        await Scripting.sleep(350);
        if (calls.length) throw new Error('Real drag wrote before release');
        pointer.notify_button(GLib.get_monotonic_time(), 1, Clutter.ButtonState.RELEASED);
        await Scripting.sleep(150);
        if (calls.length !== 1 || calls[0][0] !== 'keyboard_brightness' || Number(calls[0][1]) < 3)
            throw new Error('Real pointer slider drag failed');
        // Polling updates must preserve all actors and the open menu.
        const before = ext._buttons[0].button;
        ext._sync();
        if (before !== ext._buttons[0].button || !ext._button.menu.isOpen) throw new Error('Menu rebuilt');
        const keyboardStream = Gio.File.new_for_path(GLib.getenv('MECHREVO_UI_SCREENSHOT').replace('.png', '-keyboard.png')).replace(null, false, Gio.FileCreateFlags.NONE, null);
        await new Shell.Screenshot().screenshot(false, keyboardStream);
        keyboardStream.close(null);
        ext._keyboardMenu.setSubmenuShown(false);
        await Scripting.sleep(300);
        const stream = Gio.File.new_for_path(GLib.getenv('MECHREVO_UI_SCREENSHOT') || '/tmp/mechrevo-menu-preview.png').replace(null, false, Gio.FileCreateFlags.NONE, null);
        const [menuX, menuY] = ext._button.menu.actor.get_transformed_position();
        const [menuWidth, menuHeight] = ext._button.menu.actor.get_transformed_size();
        await new Shell.Screenshot().screenshot_area(Math.floor(menuX), Math.floor(menuY), Math.ceil(menuWidth), Math.ceil(menuHeight), stream);
        stream.close(null);
        ext._data.vendor.fan_control = 'curve';
        ext._sync();
        if (!ext._curveInfo.visible || ext._sliders.some(e => e.setting.startsWith('fan') && e.row.visible))
            throw new Error('Curve layout still exposes manual sliders');
        await Scripting.sleep(150);
        if (ext._button.menu.actor.width !== collapsedWidth)
            throw new Error('Switching to curve mode changes popup width');
        ext._keyboardMenu.setSubmenuShown(true);
        await Scripting.sleep(300);
        if (ext._button.menu.actor.width !== collapsedWidth)
            throw new Error('Keyboard submenu changes curve popup width');
        ext._keyboardMenu.setSubmenuShown(false);
        await Scripting.sleep(300);
        if (ext._button.menu.actor.width !== collapsedWidth)
            throw new Error('Collapsing keyboard changes curve popup width');
        const curvePath = (GLib.getenv('MECHREVO_UI_SCREENSHOT') || '/tmp/mechrevo-menu-preview.png').replace('.png', '-curve.png');
        const curveStream = Gio.File.new_for_path(curvePath).replace(null, false, Gio.FileCreateFlags.NONE, null);
        await new Shell.Screenshot().screenshot(false, curveStream);
        curveStream.close(null);
        ext._details.setSubmenuShown(true);
        await Scripting.sleep(400);
        if (!ext._details.menu.isOpen || ext._details.menu.actor.height <= 0) throw new Error('Details did not open');
        const detailPath = (GLib.getenv('MECHREVO_UI_SCREENSHOT') || '/tmp/mechrevo-menu-preview.png').replace('.png', '-details.png');
        const detailStream = Gio.File.new_for_path(detailPath).replace(null, false, Gio.FileCreateFlags.NONE, null);
        await new Shell.Screenshot().screenshot(false, detailStream);
        detailStream.close(null);
        print(`PASS: ${ext._buttons.length} control buttons and ${ext._sliders.length} sliders dispatch; stable menu; details opens; main menu fits`);
    } finally { ext.disable(); }
}
