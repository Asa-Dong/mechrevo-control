// SPDX-License-Identifier: GPL-2.0-only
import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import St from 'gi://St';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as Slider from 'resource:///org/gnome/shell/ui/slider.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';

const NAME = 'io.github.mechrevo.Control1';
const PATH = '/io/github/mechrevo/Control1';

export default class MechrevoControl extends Extension {
    enable() {
        this._active = true;
        this._cancellable = new Gio.Cancellable();
        this._bus = Gio.DBus.system;
        this._pending = false;
        this._writing = false;
        this._data = null;
        this._error = '';
        this._buttons = [];
        this._sliders = [];
        this._sliderTimers = new Map();
        this._queued = new Map();
        this._syncing = false;
        this._button = new PanelMenu.Button(0.0, 'Mechrevo Control');
        this._label = new St.Label({text: '硬件 …', y_align: Clutter.ActorAlign.CENTER});
        this._button.add_child(this._label);
        Main.panel.addToStatusArea(this.uuid, this._button);
        this._buildMenu();
        this._button.menu.connect('open-state-changed', (_menu, open) => {
            if (open) this._refresh();
        });
        this._refresh();
        this._timer = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 5, () => {
            this._refresh();
            return GLib.SOURCE_CONTINUE;
        });
    }

    _row(text, menu = this._button.menu) {
        const row = new PopupMenu.PopupMenuItem(text, {reactive: false, can_focus: false});
        menu.addMenuItem(row);
        return row;
    }

    _choices(title, setting, choices) {
        // Keep actors alive while interacting; only update selection and sensitivity.
        const row = new PopupMenu.PopupBaseMenuItem({reactive: false, can_focus: false, activate: false});
        row.add_child(new St.Label({text: title, x_expand: true, y_align: Clutter.ActorAlign.CENTER}));
        const box = new St.BoxLayout({style: 'spacing: 6px;'});
        for (const [value, label] of choices) {
            const button = new St.Button({
                label, style_class: 'button', can_focus: true, reactive: true,
                accessible_name: `${title}：${label}`, style: 'padding: 6px 10px; border-radius: 8px;',
            });
            button.connect('clicked', () => this._set(setting, value));
            box.add_child(button);
            this._buttons.push({button, setting, value});
        }
        row.add_child(box);
        this._button.menu.addMenuItem(row);
    }

    _slider(title, setting, minimum, maximum, channel = null) {
        const row = new PopupMenu.PopupBaseMenuItem({reactive: false, can_focus: false, activate: false});
        row.add_child(new St.Label({text: title, style: 'min-width: 72px;', y_align: Clutter.ActorAlign.CENTER}));
        const slider = new Slider.Slider(0);
        slider.style = 'min-width: 150px;';
        slider.accessible_name = title;
        row.add_child(slider);
        const label = new St.Label({text: '-', style: 'min-width: 36px;', y_align: Clutter.ActorAlign.CENTER, x_align: Clutter.ActorAlign.END});
        row.add_child(label);
        const entry = {row, slider, label, setting, minimum, maximum, channel, dirty: false, dragging: false};
        this._sliders.push(entry);
        const update = () => {
            const n = Math.round(minimum + slider.value * (maximum - minimum));
            label.text = setting.startsWith('fan') ? `${n}%` : `${n}`;
            if (this._syncing) return;
            entry.dirty = true;
            this._error = '';
            if (!entry.dragging) this._scheduleSlider(entry);
        };
        slider.connect('notify::value', update);
        slider.connect('drag-begin', () => {
            entry.dragging = true;
            this._cancelSliderTimer(setting);
        });
        slider.connect('drag-end', () => {
            entry.dragging = false;
            // Snap to supported hardware steps, then send only the released value.
            const n = Math.round(minimum + slider.value * (maximum - minimum));
            slider.value = (n - minimum) / (maximum - minimum);
            if (entry.dirty) this._scheduleSlider(entry, 0);
        });
        this._button.menu.addMenuItem(row);
    }

    _cancelSliderTimer(setting) {
        const timer = this._sliderTimers.get(setting);
        if (timer) GLib.source_remove(timer);
        this._sliderTimers.delete(setting);
    }

    _scheduleSlider(entry, delay = 300) {
        this._cancelSliderTimer(entry.setting);
        this._sliderTimers.set(entry.setting, GLib.timeout_add(GLib.PRIORITY_DEFAULT, delay, () => {
            this._sliderTimers.delete(entry.setting);
            const number = e => Math.round(e.minimum + e.slider.value * (e.maximum - e.minimum));
            const value = entry.setting === 'keyboard_rgb'
                ? this._sliders.filter(e => e.setting === 'keyboard_rgb').map(number).join(' ')
                : String(number(entry));
            this._set(entry.setting, value);
            return GLib.SOURCE_REMOVE;
        }));
    }

    _buildMenu() {
        this._summary = this._row('正在读取硬件…');
        this._gpu = this._row('独显 -');
        this._button.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this._choices('性能模式', 'mode', [['office', '办公'], ['balanced', '均衡'], ['turbo', '狂暴']]);
        this._choices('风扇控制', 'fan_control', [['auto', '自动'], ['manual', '手动'], ['curve', '曲线']]);
        this._slider('CPU 风扇', 'fan1_percent', 0, 100);
        this._slider('独显风扇', 'fan2_percent', 0, 100);
        this._curveInfo = new PopupMenu.PopupMenuItem('编辑风扇曲线…');
        this._curveInfo.connect('activate', () => {
            this._button.menu.close();
            this.openPreferences();
        });
        this._button.menu.addMenuItem(this._curveInfo);
        this._slider('键盘亮度', 'keyboard_brightness', 0, 4);
        this._slider('红 R', 'keyboard_rgb', 0, 50, 0);
        this._slider('绿 G', 'keyboard_rgb', 0, 50, 1);
        this._slider('蓝 B', 'keyboard_rgb', 0, 50, 2);
        this._choices('电池模式', 'charging_profile', [['high_capacity', '长效'], ['balanced', '平衡'], ['stationary', '工作站']]);
        this._feedback = this._row('');
        this._feedback.label.style = 'font-size: 0.9em; min-height: 22px;';
        this._details = new PopupMenu.PopupSubMenuMenuItem('更多信息');
        this._button.menu.addMenuItem(this._details);
        this._details.menu.actor.style = 'max-height: 220px;';
        this._details.menu.actor.connect('notify::vscrollbar-policy', actor => {
            if (actor.vscrollbar_policy !== St.PolicyType.AUTOMATIC)
                actor.vscrollbar_policy = St.PolicyType.AUTOMATIC;
        });
        this._details.menu.connect('open-state-changed', (_menu, open) => {
            if (open) this._updateDetails();
        });
        this._detailRows = [];
        this._chargeDetail = this._row('', this._details.menu);
        const editCurve = new PopupMenu.PopupMenuItem('编辑风扇曲线…');
        editCurve.connect('activate', () => {
            this._button.menu.close();
            this.openPreferences();
        });
        this._details.menu.addMenuItem(editCurve);
        this._release = new PopupMenu.PopupMenuItem('恢复默认值');
        this._release.connect('activate', () => this._set('control', 'release'));
        this._details.menu.addMenuItem(this._release);
        this._sync();
    }

    _updateDetails() {
        if (!this._data) return;
        // Show useful device readings; CPU is already in the summary.
        // Prefer the SSD composite reading over its internal sensor channels.
        const deviceName = t => {
            if (t.chip === 'amdgpu' && t.label === 'edge') return '核显温度';
            if (t.chip === 'mechrevo' && /GPU/i.test(t.label)) return '独显温度';
            if (t.chip === 'nvme' && t.label === 'Composite') return '固态硬盘温度';
            if (/^(spd5118|jc42)$/.test(t.chip)) return '内存温度';
            if (/^(mt\d+|iwlwifi)/.test(t.chip)) return '无线网卡温度';
            if (/^r8169/.test(t.chip)) return '有线网卡温度';
            return null;
        };
        const temps = this._data.temperatures
            .map(t => ({...t, name: deviceName(t)}))
            .filter(t => t.name && Number.isFinite(t.celsius))
            .sort((a, b) => a.name.localeCompare(b.name, 'zh-CN'));
        const counts = new Map();
        const indices = new Map();
        for (const t of temps) counts.set(t.name, (counts.get(t.name) ?? 0) + 1);
        while (this._detailRows.length < temps.length)
            this._detailRows.push(this._row('', this._details.menu));
        this._detailRows.forEach((row, i) => {
            row.visible = i < temps.length;
            if (row.visible) {
                const t = temps[i];
                const index = (indices.get(t.name) ?? 0) + 1;
                indices.set(t.name, index);
                const name = counts.get(t.name) > 1 ? `${t.name} ${index}` : t.name;
                row.label.text = `${name}  ${t.celsius.toFixed(1)}°C`;
            }
        });
    }

    _sync() {
        const d = this._data;
        const v = d?.vendor;
        const writable = Boolean(v && this._bus === Gio.DBus.system && !this._writing);
        for (const {button, setting, value} of this._buttons) {
            button.reactive = writable && v?.[setting] != null;
            button.can_focus = button.reactive;
            button.opacity = this._bus === Gio.DBus.system && v?.[setting] != null ? 255 : 120;
            const selected = String(v?.[setting]) === value;
            button.style = 'padding: 6px 10px; border-radius: 8px;' + (selected ? ' background-color: #3584e4; color: white;' : '');
            if (selected) button.add_style_pseudo_class('checked');
            else button.remove_style_pseudo_class('checked');
        }
        this._syncing = true;
        for (const e of this._sliders) {
            const supported = v && v[e.setting] != null;
            e.row.visible = !e.setting.startsWith('fan') || v?.fan_control === 'manual';
            e.slider.reactive = Boolean(writable && supported);
            e.slider.can_focus = e.slider.reactive;
            e.slider.opacity = this._bus === Gio.DBus.system && supported ? 255 : 120;
            if (!e.dirty && !e.dragging && supported) {
                const n = e.channel === null ? Number(v[e.setting]) : Number(v[e.setting].split(' ')[e.channel]);
                e.slider.value = Math.max(0, Math.min(1, (n - e.minimum) / (e.maximum - e.minimum)));
                e.label.text = e.setting.startsWith('fan') ? `${n}%` : String(n);
            }
        }
        this._syncing = false;
        this._curveInfo.visible = v?.fan_control === 'curve';
        const chargeProfile = {high_capacity: '长效', balanced: '平衡', stationary: '工作站'}[v?.charging_profile];
        this._chargeDetail.label.text = chargeProfile
            ? `充电：${chargeProfile} · 实际限充效果待验证`
            : '充电配置不可用';

        this._release.setSensitive(writable);
        const message = this._writing ? '正在应用设置…' : this._error || (v?.control === 'restore_failed' ? '控制出错，请手动恢复' : '') || (!v ? '控制驱动不可用' : this._bus !== Gio.DBus.system ? '只读模式：系统服务未连接' : '');
        // Reserve one line: showing/hiding feedback moves the whole popup.
        this._feedback.label.text = message || ' ';
        this._feedback.visible = true;
        if (!d) return;
        const temp = d.cpu_temperature?.celsius;
        const fans = d.fans.filter(f => f.chip === 'mechrevo');
        // Protocol mapping: Main = CPU, Secondary = GPU; never rely on array order.
        const rpm = label => {
            const value = fans.find(f => f.label === label)?.rpm;
            return Number.isFinite(value) ? `${value} RPM` : '- RPM';
        };
        const watts = n => Number.isFinite(n) ? `${n.toFixed(1)} W` : '-';
        this._summary.label.text = `CPU  ${temp == null ? '-' : Math.round(temp) + '°C'}  ·  ${watts(d.cpu_power_watts)}  ·  ${rpm('Main')}`;
        const gpuState = {suspended: '已休眠', transitioning: '电源切换中', unavailable: '读数不可用'}[d.gpu?.state];
        this._gpu.label.text = gpuState ? `独显  ${gpuState}` : `独显  ${d.gpu?.celsius == null ? '-' : Math.round(d.gpu.celsius) + '°C'}  ·  ${watts(d.gpu?.watts)}`;
        this._gpu.label.text += `  ·  ${rpm('Secondary')}`;
        this._label.text = `${temp == null ? '硬件' : Math.round(temp) + '°C'}${fans.length ? ' · ' + fans.map(f => f.rpm).join('/') + ' RPM' : ''}`;
        if (this._details.menu.isOpen) this._updateDetails();
    }

    _refresh(preferSystem = true) {
        if (this._pending || this._writing || !this._active) return;
        if (preferSystem) this._bus = Gio.DBus.system;
        this._pending = true;
        const cancellable = this._cancellable;
        const bus = this._bus;
        bus.call(NAME, PATH, NAME, 'GetStatus', null, new GLib.VariantType('(s)'),
            Gio.DBusCallFlags.NONE, 5000, cancellable, (connection, result) => {
                let reply, error;
                try { reply = connection.call_finish(result); } catch (e) { error = e; }
                if (!this._active || this._cancellable !== cancellable) return;
                this._pending = false;
                if (error) {
                    const remote = Gio.DBusError.get_remote_error(error);
                    if (bus === Gio.DBus.system && ['org.freedesktop.DBus.Error.ServiceUnknown', 'org.freedesktop.DBus.Error.NameHasNoOwner'].includes(remote)) {
                        this._bus = Gio.DBus.session;
                        this._refresh(false);
                        return;
                    }
                    this._error = '硬件读取失败，请稍后重试';
                    this._data = null;
                    this._label.text = '硬件离线';
                } else {
                    try { this._data = JSON.parse(reply.deep_unpack()[0]); }
                    catch (_e) { this._error = '硬件数据格式异常'; this._data = null; }
                }
                this._sync();
            });
    }

    _set(setting, value) {
        if (!this._data?.vendor || this._bus !== Gio.DBus.system) return;
        if (this._writing) { this._queued.set(setting, value); return; }
        this._writing = true;
        this._error = '';
        this._sync();
        const cancellable = this._cancellable;
        Gio.DBus.system.call(NAME, PATH, NAME, 'Set', new GLib.Variant('(ss)', [setting, value]),
            new GLib.VariantType('()'), Gio.DBusCallFlags.ALLOW_INTERACTIVE_AUTHORIZATION,
            120000, cancellable, (connection, result) => {
                let error;
                try { connection.call_finish(result); } catch (e) { error = e; }
                if (!this._active || this._cancellable !== cancellable) return;
                this._writing = false;
                if (!error && this._data?.vendor && setting !== 'charging_profile') {
                    this._data.vendor[setting] = ['keyboard_brightness', 'fan1_percent', 'fan2_percent'].includes(setting) ? Number(value) : value;
                }
                for (const e of this._sliders) {
                    if (e.setting === setting && !this._queued.has(setting) && !this._sliderTimers.has(setting)) e.dirty = false;
                }
                if (error) {
                    const remote = Gio.DBusError.get_remote_error(error);
                    this._error = remote === 'org.freedesktop.DBus.Error.AccessDenied' ? '未获授权，请完成管理员认证后重试' : '设置失败，请重试';
                    console.error(`[Mechrevo Control] ${error.message}`);
                    Main.notify('Mechrevo Control', this._error);
                }
                if (error) {
                    for (const timer of this._sliderTimers.values()) GLib.source_remove(timer);
                    this._sliderTimers.clear();
                    this._queued.clear();
                    for (const e of this._sliders) e.dirty = false;
                }
                this._sync();
                if (this._queued.size) {
                    const [key, next] = this._queued.entries().next().value;
                    this._queued.delete(key);
                    this._set(key, next);
                } else this._refresh();
            });
    }

    disable() {
        this._active = false;
        if (this._timer) GLib.source_remove(this._timer);
        this._timer = null;
        for (const timer of this._sliderTimers.values()) GLib.source_remove(timer);
        this._sliderTimers.clear();
        this._queued.clear();
        this._cancellable?.cancel();
        this._cancellable = null;
        this._button?.destroy();
        this._button = null;
        this._buttons = [];
        this._detailRows = [];
        this._data = null;
    }
}
