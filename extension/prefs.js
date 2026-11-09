// SPDX-License-Identifier: GPL-2.0-only
import {ExtensionPreferences} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';
import Adw from 'gi://Adw';
import Gtk from 'gi://Gtk?version=4.0';
import Gio from 'gi://Gio';
import {CurveEditor} from './curveEditor.js';
export default class Preferences extends ExtensionPreferences {
    fillPreferencesWindow(window) {
        const settings = this.getSettings();
        const page = new Adw.PreferencesPage({title: '键盘', icon_name: 'input-keyboard-symbolic'});
        const group = new Adw.PreferencesGroup({title: '键盘背光', description: '桌面无操作时关灯，键盘或鼠标活动后恢复。需要保持扩展启用。'});
        const adjustment = new Gtk.Adjustment({lower: 0, upper: 300, step_increment: 1, page_increment: 10});
        const seconds = new Gtk.Scale({orientation: Gtk.Orientation.HORIZONTAL, adjustment, digits: 0, draw_value: true, hexpand: true, width_request: 240, valign: Gtk.Align.CENTER});
        seconds.set_format_value_func((_scale, value) => value === 0 ? '关闭' : `${Math.round(value)} 秒`);
        const row = new Adw.ActionRow({title: '自动关灯', subtitle: '0 表示不自动关灯；最多 300 秒'});
        row.add_suffix(seconds);
        settings.bind('keyboard-idle-seconds', adjustment, 'value', Gio.SettingsBindFlags.DEFAULT);
        group.add(row);
        page.add(group);
        window.add(page);
        this.editor = new CurveEditor(window);
        window.title = '机械革命设置';
    }
}
