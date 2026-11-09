// SPDX-License-Identifier: GPL-2.0-only
import Adw from 'gi://Adw';
import Gtk from 'gi://Gtk?version=4.0';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
const NAME = 'io.github.mechrevo.Control1';
const PATH = '/io/github/mechrevo/Control1';
export const DEFAULTS = [30, 40, 60, 80, 100, 30, 40, 60, 80, 100];
export function validCurve(values, minimum = 0) {
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
        this.points = [];
        this.charts = [];
        this.drags = [];
        this.dragPoint = null;
        window.set_default_size(720, 740);
        window.title = '风扇曲线';
        window.connect('close-request', () => {
            if (this.busy) return true;
            this.closed = true;
            return false;
        });
        const page = new Adw.PreferencesPage({title: '风扇曲线', icon_name: 'preferences-system-symbolic'});
        window.add(page);
        for (let fan = 0; fan < 2; fan++) {
            const name = fan === 0 ? 'CPU 风扇' : 'GPU 风扇';
            const group = new Adw.PreferencesGroup({title: name,
                description: '上下拖动圆点调节 0–100%。温区固定，转速随温度不递减。'});
            page.add(group);
            const chart = new Gtk.DrawingArea({content_height: 160, hexpand: true});
            chart.set_tooltip_text(`${name}：拖动圆点调整百分比，点击应用后生效`);
            chart.set_draw_func((_area, cr, w, h) => this.draw(cr, w, h, fan));
            this.charts.push(chart);
            group.add(chart);
            const drag = new Gtk.GestureDrag({button: 1, propagation_phase: Gtk.PropagationPhase.CAPTURE});
            drag.connect('drag-begin', (gesture, x, y) => {
                if (!this.beginDrag(fan, x, y)) {
                    gesture.set_state(Gtk.EventSequenceState.DENIED);
                    return;
                }
                gesture.set_state(Gtk.EventSequenceState.CLAIMED);
            });
            drag.connect('drag-update', (_gesture, _dx, dy) => this.updateDrag(fan, dy));
            drag.connect('drag-end', (_gesture, _dx, dy) => {
                this.updateDrag(fan, dy);
                this.dragPoint = null;
            });
            drag.connect('cancel', () => { this.dragPoint = null; });
            chart.add_controller(drag);
            this.drags.push(drag);
            for (let i = 0; i < 5; i++) {
                const point = new Gtk.Adjustment({lower: 0, upper: 100, step_increment: 1});
                this.points[fan * 5 + i] = point;
                point.connect('value-changed', () => {
                    chart.queue_draw();
                    if (!this.loading && this.feedback)
                        this.feedback.label = validCurve(this.values()) ? '有未应用的修改' : '每路百分比需随温度不递减';
                    this.sync();
                });
            }
        }
        const actions = new Adw.PreferencesGroup({description: '每路 5 个温区，更高温度沿用末档值。0% 不保证停转，实际转速以读数为准。应用后切换到曲线模式，保存并在服务启动时恢复；可与三档性能模式同时使用；恢复默认值会退出曲线。'});
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
    values() { return this.points.map(s => Math.round(s.get_value())); }
    populate(values) {
        this.loading = true;
        this.points.forEach((s, i) => s.set_value(values[i]));
        this.loading = false;
        this.sync();
        this.charts.forEach(chart => chart.queue_draw());
    }
    sync() {
        if (!this.applyButton) return;
        this.charts.forEach(chart => { chart.sensitive = this.ready && !this.busy; });
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
            this.feedback.label = current.mode === 'curve' ? '已读取 · 曲线正在生效' : '已读取 · 当前未启用曲线';
        } catch (e) { this.ready = false; this.feedback.label = `读取失败：${e.message}`; }
        finally { this.busy = false; this.sync(); }
    }
    async apply() {
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
    geometry(w, h) {
        const left = 44, right = Math.max(left + 1, w - 24), top = 24, bottom = Math.max(top + 1, h - 30);
        return {left, right, top, bottom,
            x: t => left + (t - 20) / 70 * (right - left),
            y: p => bottom - p / 100 * (bottom - top)};
    }
    beginDrag(fan, x, y) {
        this.dragPoint = null;
        if (!this.ready || this.busy) return false;
        const chart = this.charts[fan];
        const g = this.geometry(chart.get_width(), chart.get_height());
        const values = this.values();
        let nearest = -1, distance = 20;
        for (let i = 0; i < 5; i++) {
            const d = Math.hypot(x - g.x(40 + i * 10), y - g.y(values[fan * 5 + i]));
            if (d < distance) { nearest = i; distance = d; }
        }
        if (nearest < 0) return false;
        this.dragPoint = {fan, index: nearest, y, height: g.bottom - g.top, bottom: g.bottom};
        return true;
    }
    updateDrag(fan, dy) {
        const point = this.dragPoint;
        if (!point || point.fan !== fan || !this.ready || this.busy) return;
        const index = fan * 5 + point.index;
        const values = this.values();
        const lower = point.index === 0 ? 0 : values[index - 1];
        const percent = Math.round((point.bottom - point.y - dy) / point.height * 100);
        const target = Math.max(lower, Math.min(100, percent));
        // Raise later nodes first so every intermediate curve remains valid.
        for (let i = fan * 5 + 4; i > index; i--) {
            if (values[i] < target) this.points[i].set_value(target);
        }
        this.points[index].set_value(target);
    }
    draw(cr, w, h, fan) {
        const {left, right, top, bottom, x, y} = this.geometry(w, h);
        const color = this.charts[fan].get_color();
        cr.setLineWidth(1);
        cr.setSourceRGBA(color.red, color.green, color.blue, 0.3);
        for (const p of [0, 25, 50, 75, 100]) {
            cr.moveTo(left, y(p)); cr.lineTo(right, y(p)); cr.stroke();
            cr.moveTo(4, y(p) + 4); cr.showText(`${p}%`);
        }
        for (const t of [20, 40, 50, 60, 70, 80, 90]) {
            cr.moveTo(x(t), top); cr.lineTo(x(t), bottom); cr.stroke();
            cr.moveTo(x(t) - 10, h - 8); cr.showText(`${t}°`);
        }
        if (this.points.length !== 10 || this.points.some(spin => !spin)) return;
        const values = this.values().slice(fan * 5, fan * 5 + 5);
        cr.setSourceRGB(...(fan ? [0.92, 0.48, 0.12] : [0.2, 0.52, 0.9]));
        cr.setLineWidth(2.5);
        cr.moveTo(x(20), y(0));
        for (let i = 0; i < 5; i++) {
            cr.lineTo(x(40 + i * 10), y(i === 0 ? 0 : values[i - 1]));
            cr.lineTo(x(40 + i * 10), y(values[i]));
        }
        cr.lineTo(x(90), y(values[4])); cr.stroke();
        for (let i = 0; i < 5; i++) {
            cr.arc(x(40 + i * 10), y(values[i]), 7, 0, Math.PI * 2); cr.fill();
            cr.moveTo(x(40 + i * 10) - 12, y(values[i]) - 11);
            cr.showText(`${values[i]}%`);
        }
    }
}
