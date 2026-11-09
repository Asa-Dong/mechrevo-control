// SPDX-License-Identifier: GPL-2.0-only
import Adw from 'gi://Adw';
import Gtk from 'gi://Gtk?version=4.0';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
const NAME = 'io.github.mechrevo.Control1';
const PATH = '/io/github/mechrevo/Control1';
export const DEFAULTS = [30, 40, 60, 80, 100, 30, 40, 60, 80, 100];
export function validCurve(values, minimum = 20) {
    return values.length === 10 && values.every((n, i) => Number.isInteger(n) && n >= minimum && n <= 100 &&
        (i % 5 === 0 || n >= values[i - 1]));
}
function request(method, values = null) {
    return new Promise((resolve, reject) => Gio.DBus.system.call(NAME, PATH, NAME, method,
        values ? new GLib.Variant('(ss)', values) : null,
        new GLib.VariantType(method === 'Set' ? '()' : '(s)'),
        Gio.DBusCallFlags.ALLOW_INTERACTIVE_AUTHORIZATION, 120000, null, (bus, result) => {
            try { resolve(bus.call_finish(result).deep_unpack()); } catch (e) { reject(e); }
        }));
}
export class CurveEditor {
    constructor(window, rpc = request) {
        this.window = window;
        this.rpc = rpc;
        this.busy = false;
        this.ready = false;
        this.closed = false;
        this.spins = [];
        window.set_default_size(660, 820);
        window.title = '风扇曲线';
        window.connect('close-request', () => {
            if (this.busy) return true;
            this.closed = true;
            return false;
        });
        const page = new Adw.PreferencesPage({title: '风扇曲线', icon_name: 'preferences-system-symbolic'});
        window.add(page);
        const chartGroup = new Adw.PreferencesGroup({title: '温度 → 风扇转速',
            description: '蓝色：CPU 风扇 · 橙色：GPU 风扇。固定温区，百分比随温度不递减。'});
        page.add(chartGroup);
        this.chart = new Gtk.DrawingArea({content_height: 140, hexpand: true});
        this.chart.set_draw_func((_area, cr, w, h) => this.draw(cr, w, h));
        chartGroup.add(this.chart);
        const group = new Adw.PreferencesGroup({title: '两路独立设置',
            description: '每档 20–100%；更高温区沿用末档值。本机实测低于 25% 时仍输出约 25%。降温时低 3°C 换档，避免频繁变速。'});
        page.add(group);
        for (let i = 0; i < 5; i++) {
            const row = new Adw.ActionRow({title: `${40 + i * 10}°C 温区上限`});
            for (let fan = 0; fan < 2; fan++) {
                const spin = new Gtk.SpinButton({adjustment: new Gtk.Adjustment({lower: 20, upper: 100, step_increment: 5, page_increment: 10}),
                    numeric: true, digits: 0, valign: Gtk.Align.CENTER, width_chars: 3});
                spin.set_tooltip_text(`风扇 ${fan + 1} · ${40 + i * 10}°C · 百分比`);
                const box = new Gtk.Box({spacing: 6, valign: Gtk.Align.CENTER});
                box.append(new Gtk.Label({label: `${fan + 1}`}));
                box.append(spin);
                box.append(new Gtk.Label({label: '%'}));
                row.add_suffix(box);
                this.spins[fan * 5 + i] = spin;
                spin.connect('value-changed', () => {
                    this.chart.queue_draw();
                    if (!this.loading) this.feedback.label = validCurve(this.values()) ? '有未应用的修改' : '每路百分比需随温度不递减';
                    this.sync();
                });
            }
            group.add(row);
        }
        const actions = new Adw.PreferencesGroup({description: '应用后立即切换到曲线模式，由 EC 自主调速。切换性能模式或恢复设置会退出曲线；不保证重启保留。'});
        page.add(actions);
        const buttons = new Gtk.Box({spacing: 10, homogeneous: true});
        this.reload = new Gtk.Button({label: '重新读取'});
        this.defaults = new Gtk.Button({label: '默认曲线'});
        this.applyButton = new Gtk.Button({label: '应用到固件', css_classes: ['suggested-action']});
        for (const b of [this.reload, this.defaults, this.applyButton]) buttons.append(b);
        actions.add(buttons);
        this.feedback = new Gtk.Label({label: '正在读取…', wrap: true, margin_top: 12, margin_bottom: 8});
        actions.add(this.feedback);
        this.reload.connect('clicked', () => this.load());
        this.defaults.connect('clicked', () => { this.populate(DEFAULTS); this.feedback.label = '默认曲线已填入，点击应用后生效'; });
        this.applyButton.connect('clicked', () => this.apply());
        this.populate(DEFAULTS);
        this.load();
    }
    values() { return this.spins.map(s => s.get_value_as_int()); }
    populate(values) {
        this.loading = true;
        this.spins.forEach((s, i) => s.set_value(values[i]));
        this.loading = false;
        this.sync();
        this.chart.queue_draw();
    }
    sync() {
        if (!this.applyButton) return;
        this.spins.forEach((s, i) => { s.sensitive = this.ready && !this.busy; });
        this.applyButton.sensitive = this.ready && !this.busy && validCurve(this.values());
        this.defaults.sensitive = this.ready && !this.busy;
        this.reload.sensitive = !this.busy;
    }
    async read() {
        const result = await this.rpc('GetStatus');
        const v = JSON.parse(result[0]).vendor;
        const values = (v?.fan_curve ?? '').trim().split(/\s+/).map(Number);
        if (!validCurve(values, 0)) throw new Error('固件曲线接口不可用');
        return {values, mode: v.fan_control};
    }
    async load() {
        if (this.busy) return;
        this.busy = true;
        this.sync();
        try {
            const current = await this.read();
            if (this.closed) return;
            this.ready = true;
            this.populate(current.values);
            this.feedback.label = current.values.some(v => v < 20) ? '已读取；低于 20% 的值在编辑草稿中显示为 20%，点击应用后才写入' : current.mode === 'curve' ? '已读取 · 曲线正在生效' : '已读取 · 当前未启用曲线';
        } catch (e) { this.ready = false; this.feedback.label = `读取失败：${e.message}`; }
        finally { this.busy = false; this.sync(); }
    }
    async apply() {
        this.spins.forEach(s => s.update());
        const values = this.values();
        if (this.busy || !this.ready || !validCurve(values)) return;
        this.busy = true;
        this.feedback.label = '正在写入固件，请稍候…';
        this.sync();
        try {
            await this.rpc('Set', ['fan_curve', values.join(' ')]);
            const current = await this.read();
            if (current.mode !== 'curve' || values.some((v, i) => v !== current.values[i])) throw new Error('读回结果不一致，请重新读取');
            this.feedback.label = '已应用 · 两路风扇由 EC 按曲线自动调速';
        } catch (e) { this.feedback.label = `应用未确认：${e.message}。可重新读取当前状态`; }
        finally { this.busy = false; this.sync(); }
    }
    draw(cr, w, h) {
        const left = 44, right = w - 18, top = 16, bottom = h - 30;
        const x = t => left + (t - 20) / 70 * (right - left);
        const y = p => bottom - p / 100 * (bottom - top);
        cr.setLineWidth(1);
        cr.setSourceRGBA(0.5, 0.5, 0.5, 0.45);
        for (const p of [0, 50, 100]) {
            cr.moveTo(left, y(p)); cr.lineTo(right, y(p)); cr.stroke();
            cr.moveTo(4, y(p) + 4); cr.showText(`${p}%`);
        }
        for (const t of [20, 40, 60, 80]) { cr.moveTo(x(t) - 8, h - 8); cr.showText(`${t}°`); }
        if (this.spins.length !== 10 || this.spins.some(s => !s)) return;
        const values = this.values();
        for (let fan = 0; fan < 2; fan++) {
            cr.setSourceRGB(...(fan ? [0.95, 0.55, 0.15] : [0.2, 0.52, 0.9]));
            cr.setLineWidth(fan ? 2 : 3);
            cr.setDash(fan ? [6, 3] : [], 0);
            cr.moveTo(x(20), y(values[fan * 5]));
            for (let i = 0; i < 5; i++) {
                cr.lineTo(x(40 + i * 10), y(values[fan * 5 + i]));
                if (i < 4) cr.lineTo(x(40 + i * 10), y(values[fan * 5 + i + 1]));
            }
            cr.lineTo(x(90), y(values[fan * 5 + 4])); cr.stroke();
        }
    }
}
