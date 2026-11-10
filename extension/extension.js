// SPDX-License-Identifier: GPL-2.0-only
import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import St from 'gi://St';
import {KeyboardIdle} from './keyboardIdle.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as Slider from 'resource:///org/gnome/shell/ui/slider.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';

const NAME = 'io.github.mechrevo.Control1';
const PATH = '/io/github/mechrevo/Control1';
// Soften readouts and captions without dimming their interactive controls.
const SECONDARY_TEXT_OPACITY = 205;

export default class MechrevoControl extends Extension {
    enable() {
        this._active = true;
        this._cancellable = new Gio.Cancellable();
        this._bus = Gio.DBus.system;
        this._brightnessRevision = 0;
        this._settingsRevision = 0;
        this._pending = false;
        this._writing = false;
        this._data = null;
        this._error = '';
        this._readError = '';
        this._buttons = [];
        this._sliders = [];
        this._sliderTimers = new Map();
        this._queued = new Map();
        this._syncing = false;
        this._button = new PanelMenu.Button(0.0, 'Mechrevo Control');
        this._button.menu.setSourceAlignment(0.0);
        // Reserve the expanded keyboard layout even when its rows are hidden.
        this._button.menu.box.style = 'width: 320px;';
        this._label = new St.Label({text: '硬件 …', y_align: Clutter.ActorAlign.CENTER});
        this._button.add_child(this._label);
        Main.panel.addToStatusArea(this.uuid, this._button);
        this._settings = this.getSettings();
        this._buildMenu();
        this._settingsChanged = this._settings.connect('changed::keyboard-idle-seconds', () => this._sync());
        this._button.menu.connect('open-state-changed', (_menu, open) => {
            if (open) this._refresh();
        });
        this._keyboardIdle = new KeyboardIdle({
            idle: () => global.backend.get_core_idle_monitor().get_idletime(),
            brightness: () => this._data?.vendor?.keyboard_brightness,
            ready: () => this._bus === Gio.DBus.system && !this._writing && Boolean(this._data?.vendor),
            write: value => new Promise((resolve, reject) => {
                Gio.DBus.system.call(NAME, PATH, NAME, 'Set', new GLib.Variant('(ss)', ['keyboard_brightness', String(value)]),
                    new GLib.VariantType('()'), Gio.DBusCallFlags.NONE, 5000, null, (connection, result) => {
                        try {
                            connection.call_finish(result);
                            this._brightnessRevision++;
                            if (this._active && this._data?.vendor)
                                this._data.vendor.keyboard_brightness = value;
                            resolve();
                        } catch (e) { reject(e); }
                    });
            }),
            report: error => console.error(`[Mechrevo keyboard idle] ${error.message}`),
        });
        this._idleTimer = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 1, () => {
            this._keyboardIdle.tick(this._settings.get_uint('keyboard-idle-seconds'));
            return GLib.SOURCE_CONTINUE;
        });
        this._refresh();
        this._timer = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 5, () => {
            this._refresh();
            return GLib.SOURCE_CONTINUE;
        });
    }

    _row(text, menu = this._button.menu, position = undefined) {
        const row = new PopupMenu.PopupMenuItem(text, {reactive: false, can_focus: false});
        menu.addMenuItem(row, position);
        // Use a readable secondary tone, not the theme's disabled-control color.
        row.remove_style_pseudo_class('insensitive');
        row.label.opacity = SECONDARY_TEXT_OPACITY;
        return row;
    }

    _choices(title, setting, choices) {
        // Keep actors alive while interacting; only update selection and sensitivity.
        const row = new PopupMenu.PopupBaseMenuItem({reactive: false, can_focus: false, activate: false});
        row.add_child(new St.Label({text: title, opacity: SECONDARY_TEXT_OPACITY, x_expand: true, y_align: Clutter.ActorAlign.CENTER}));
        const box = new St.BoxLayout({style: 'spacing: 6px;'});
        for (const [value, label] of choices) {
            const button = new St.Button({
                label, style_class: 'button', can_focus: true, reactive: true, track_hover: true,
                accessible_name: `${title}：${label}${setting === 'charging_profile' ? `（${{high_capacity: 100, balanced: 80, stationary: 60}[value]}%）` : ''}`,
                style: 'padding: 6px 10px; border-radius: 8px;',
            });
            button.connect('clicked', () => this._set(setting, value));
            if (setting === 'charging_profile') {
                button.connect('notify::hover', () => {
                    this._hideChargeTooltip();
                    if (button.hover) {
                        this._chargeTooltipTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 400, () => {
                            this._chargeTooltipTimer = 0;
                            if (this._active && button.hover && this._button.menu.isOpen)
                                this._showChargeTooltip(button, value);
                            return GLib.SOURCE_REMOVE;
                        });
                    }
                });
            }
            box.add_child(button);
            this._buttons.push({button, setting, value});
        }
        row.add_child(box);
        this._button.menu.addMenuItem(row);
        row.remove_style_pseudo_class('insensitive');
    }

    _chargeTooltipText(value) {
        return {high_capacity: '上限100%', balanced: '上限80% · 75%恢复', stationary: '上限60% · 55%恢复'}[value];
    }

    _showChargeTooltip(button, value) {
        this._hideChargeTooltip();
        this._chargeTooltipValue = value;
        this._chargeTooltip = new St.Label({text: this._chargeTooltipText(value), style_class: 'dash-label', reactive: false});
        this._chargeTooltip.clutter_text.set_single_line_mode(false);
        Main.layoutManager.addTopChrome(this._chargeTooltip, {affectsInputRegion: false});
        this._chargeTooltip.show();
        const [x, y] = button.get_transformed_position();
        const [width] = button.get_transformed_size();
        const monitor = Main.layoutManager.findMonitorForActor(button) ?? Main.layoutManager.primaryMonitor;
        const [, tooltipWidth] = this._chargeTooltip.get_preferred_width(-1);
        const [, tooltipHeight] = this._chargeTooltip.get_preferred_height(tooltipWidth);
        this._chargeTooltip.set_position(
            Math.round(Math.max(monitor.x + 8, Math.min(x + width / 2 - tooltipWidth / 2, monitor.x + monitor.width - tooltipWidth - 8))),
            Math.round(Math.max(monitor.y + 8, y - tooltipHeight - 8)));
    }

    _hideChargeTooltip() {
        if (this._chargeTooltipTimer) GLib.source_remove(this._chargeTooltipTimer);
        this._chargeTooltipTimer = 0;
        if (this._chargeTooltip) {
            Main.layoutManager.removeChrome(this._chargeTooltip);
            this._chargeTooltip.destroy();
            this._chargeTooltip = null;
        }
    }

    _sliderText(setting, n) {
        if (setting === 'keyboard-idle-seconds') return n === 0 ? '关闭' : `${n} 秒`;
        return setting.startsWith('fan') ? `${n}%` : String(n);
    }

    _slider(title, setting, minimum, maximum, channel = null, menu = this._button.menu) {
        const row = new PopupMenu.PopupBaseMenuItem({reactive: false, can_focus: false, activate: false});
        row.add_child(new St.Label({text: title, opacity: SECONDARY_TEXT_OPACITY, style: 'min-width: 72px;', y_align: Clutter.ActorAlign.CENTER}));
        const slider = new Slider.Slider(0);
        slider.style = 'min-width: 150px;';
        slider.accessible_name = title;
        row.add_child(slider);
        // Every slider uses the same columns, including room for '300 秒'.
        const valueStyle = 'width: 4em; text-align: right;';
        const label = new St.Label({text: '-', opacity: SECONDARY_TEXT_OPACITY, style: valueStyle, y_align: Clutter.ActorAlign.CENTER, x_align: Clutter.ActorAlign.END});
        row.add_child(label);
        const entry = {row, slider, label, setting, minimum, maximum, channel, dirty: false, dragging: false};
        this._sliders.push(entry);
        const update = () => {
            const n = Math.round(minimum + slider.value * (maximum - minimum));
            label.text = this._sliderText(setting, n);
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
        menu.addMenuItem(row);
        row.remove_style_pseudo_class('insensitive');
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
        this._gpu = this._row('GPU -');
        this._button.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this._choices('性能模式', 'mode', [['office', '办公'], ['balanced', '均衡'], ['turbo', '狂暴']]);
        this._choices('风扇控制', 'fan_control', [['auto', '自动'], ['manual', '手动'], ['curve', '曲线']]);
        this._slider('CPU 风扇', 'fan1_percent', 0, 100);
        this._slider('GPU 风扇', 'fan2_percent', 0, 100);
        this._curveInfo = new PopupMenu.PopupMenuItem('编辑风扇曲线…');
        this._curveInfo.connect('activate', () => {
            this._button.menu.close();
            this.openPreferences();
        });
        this._button.menu.addMenuItem(this._curveInfo);
        this._keyboardMenu = new PopupMenu.PopupSubMenuMenuItem('键盘设置');
        this._button.menu.addMenuItem(this._keyboardMenu);
        const keyboardMenu = this._keyboardMenu.menu;
        keyboardMenu.actor.style = 'border-left-width: 0; border-right-width: 0;';
        this._slider('键盘亮度', 'keyboard_brightness', 0, 4, null, keyboardMenu);
        this._slider('关灯时间', 'keyboard-idle-seconds', 0, 300, null, keyboardMenu);
        this._slider('红 R', 'keyboard_rgb', 0, 50, 0, keyboardMenu);
        this._slider('绿 G', 'keyboard_rgb', 0, 50, 1, keyboardMenu);
        this._slider('蓝 B', 'keyboard_rgb', 0, 50, 2, keyboardMenu);
        this._button.menu.connect('open-state-changed', (_menu, open) => {
            if (!open) {
                this._keyboardMenu.setSubmenuShown(false);
                this._hideChargeTooltip();
            }
        });
        this._choices('电池模式', 'charging_profile', [['high_capacity', '长效'], ['balanced', '平衡'], ['stationary', '工作站']]);

        this._feedback = this._row('');
        this._feedback.label.style = 'font-size: 0.9em; min-height: 22px;';
        this._details = new PopupMenu.PopupSubMenuMenuItem('更多信息');
        this._button.menu.addMenuItem(this._details);
        this._chargeState = this._row('', this._details.menu);
        this._details.menu.actor.style = 'max-height: 220px;';
        this._details.menu.actor.connect('notify::vscrollbar-policy', actor => {
            if (actor.vscrollbar_policy !== St.PolicyType.AUTOMATIC)
                actor.vscrollbar_policy = St.PolicyType.AUTOMATIC;
        });
        this._details.menu.connect('open-state-changed', (_menu, open) => {
            if (open) this._updateDetails();
        });
        this._detailRows = [];
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
        const battery = this._data.batteries?.[0];
        const batteryStatus = {Charging: '充电中', Discharging: '放电中', Full: '已充满', 'Not charging': '未充电'}[battery?.status] ?? '状态未知';
        this._chargeState.label.text = `电量 ${battery?.percent ?? '-'}% · ${batteryStatus} · ${battery?.current_ua == null ? '-' : Math.round(battery.current_ua / 1000)} mA`;
        // Show useful device readings; CPU is already in the summary.
        // Prefer the SSD composite reading over its internal sensor channels.
        const deviceName = t => {
            if (t.chip === 'amdgpu' && t.label === 'edge') return '核显温度';
            if (t.chip === 'mechrevo' && /GPU/i.test(t.label)) return 'GPU 温度';
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
            this._detailRows.push(this._row('', this._details.menu, this._detailRows.length + 1));
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
            const supported = v?.[setting] != null && (setting !== 'charging_profile' || v?.charge_limit != null);
            button.reactive = writable && supported;
            button.can_focus = button.reactive;
            button.opacity = this._bus === Gio.DBus.system && supported ? 255 : 120;
            const selected = supported && String(v?.[setting]) === value;
            button.style = 'padding: 6px 10px; border-radius: 8px;' + (selected ? ' background-color: #3584e4; color: white;' : '');
            if (selected) button.add_style_pseudo_class('checked');
            else button.remove_style_pseudo_class('checked');
        }
        this._syncing = true;
        for (const e of this._sliders) {
            const local = e.setting === 'keyboard-idle-seconds';
            const supported = local || (v && v[e.setting] != null);
            e.row.visible = !e.setting.startsWith('fan') || v?.fan_control === 'manual';
            e.slider.reactive = Boolean((local || writable) && supported);
            e.slider.can_focus = e.slider.reactive;
            e.slider.opacity = (local || this._bus === Gio.DBus.system) && supported ? 255 : 120;
            if (!e.dirty && !e.dragging && supported) {
                const n = local ? this._settings.get_uint(e.setting)
                    : e.setting === 'keyboard_brightness' && this._keyboardIdle?.saved != null ? this._keyboardIdle.saved
                    : e.channel === null ? Number(v[e.setting]) : Number(v[e.setting].split(' ')[e.channel]);
                e.slider.value = Math.max(0, Math.min(1, (n - e.minimum) / (e.maximum - e.minimum)));
                e.label.text = this._sliderText(e.setting, n);
            }
        }
        this._syncing = false;
        this._curveInfo.visible = v?.fan_control === 'curve';

        this._release.setSensitive(writable);
        const message = this._writing ? '正在应用设置…' : this._error || this._readError || (v?.control === 'restore_failed' ? '控制出错，请手动恢复' : '') || (!v ? '控制驱动不可用' : this._bus !== Gio.DBus.system ? '只读模式：系统服务未连接' : '');
        // Reserve one line: showing/hiding feedback moves the whole popup.
        this._feedback.label.text = message || ' ';
        this._feedback.visible = true;
        if (this._chargeTooltip)
            this._chargeTooltip.text = this._chargeTooltipText(this._chargeTooltipValue);
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
        this._gpu.label.text = gpuState ? `GPU  ${gpuState}` : `GPU  ${d.gpu?.celsius == null ? '-' : Math.round(d.gpu.celsius) + '°C'}  ·  ${watts(d.gpu?.watts)}`;
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
        const brightnessRevision = this._brightnessRevision;
        const settingsRevision = this._settingsRevision;
        bus.call(NAME, PATH, NAME, 'GetStatus', null, new GLib.VariantType('(s)'),
            Gio.DBusCallFlags.NONE, 5000, cancellable, (connection, result) => {
                let reply, error;
                try { reply = connection.call_finish(result); } catch (e) { error = e; }
                if (!this._active || this._cancellable !== cancellable) return;
                this._pending = false;
                if (error && settingsRevision !== this._settingsRevision) {
                    if (!this._writing) this._refresh();
                    return;
                }
                if (error) {
                    const remote = Gio.DBusError.get_remote_error(error);
                    if (bus === Gio.DBus.system && ['org.freedesktop.DBus.Error.ServiceUnknown', 'org.freedesktop.DBus.Error.NameHasNoOwner'].includes(remote)) {
                        this._bus = Gio.DBus.session;
                        this._refresh(false);
                        return;
                    }
                    this._readError = '硬件读取失败，请稍后重试';
                    this._data = null;
                    this._label.text = '硬件离线';
                } else {
                    try {
                        const data = JSON.parse(reply.deep_unpack()[0]);
                        // A request started before a confirmed write cannot revert its result.
                        if (settingsRevision !== this._settingsRevision && this._data?.vendor)
                            data.vendor = this._data.vendor;
                        // A status read started before a write must not undo its newer brightness.
                        if (brightnessRevision !== this._brightnessRevision && data.vendor && this._data?.vendor)
                            data.vendor.keyboard_brightness = this._data.vendor.keyboard_brightness;
                        this._data = data;
                        this._readError = '';
                    }
                    catch (_e) { this._readError = '硬件数据格式异常'; this._data = null; }
                }
                this._sync();
                if (settingsRevision !== this._settingsRevision && !this._writing)
                    this._refresh();
            });
    }

    _set(setting, value) {
        if (setting === 'keyboard-idle-seconds') {
            const seconds = Number(value);
            if (!Number.isInteger(seconds) || seconds < 0 || seconds > 300) return;
            this._settings.set_uint(setting, seconds);
            for (const e of this._sliders) if (e.setting === setting) e.dirty = false;
            this._sync();
            return;
        }
        if (!this._data?.vendor || this._bus !== Gio.DBus.system) return;
        if (this._writing) { this._queued.set(setting, value); return; }
        if (this._keyboardIdle?.busy) { this._error = '键盘灯光正在切换，请稍后重试'; this._sync(); return; }
        this._settingsRevision++;
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
                if (!error && (setting === 'keyboard_brightness' || setting === 'control')) {
                    this._keyboardIdle?.forget();
                    this._brightnessRevision++;
                }
                if (!error) this._settingsRevision++;
                if (!error && this._data?.vendor) {
                    this._data.vendor[setting] = ['keyboard_brightness', 'fan1_percent', 'fan2_percent'].includes(setting) ? Number(value) : value;
                    if (setting === 'charging_profile') {
                        const limit = {high_capacity: 100, balanced: 80, stationary: 60}[value];
                        Object.assign(this._data.vendor, {
                            charge_limit: limit, charge_resume: limit === 100 ? 0 : limit - 5,
                            charge_gate: limit === 100 ? 0 : 1, charge_reached: null,
                        });
                    }
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
        if (this._idleTimer) GLib.source_remove(this._idleTimer);
        this._idleTimer = null;
        this._keyboardIdle?.stop();
        this._keyboardIdle = null;
        if (this._settingsChanged) this._settings.disconnect(this._settingsChanged);
        this._settingsChanged = null;
        this._settings = null;
        if (this._timer) GLib.source_remove(this._timer);
        this._timer = null;
        for (const timer of this._sliderTimers.values()) GLib.source_remove(timer);
        this._sliderTimers.clear();
        this._queued.clear();
        this._cancellable?.cancel();
        this._cancellable = null;
        this._hideChargeTooltip();
        this._button?.destroy();
        this._button = null;
        this._buttons = [];
        this._detailRows = [];
        this._data = null;
    }
}
