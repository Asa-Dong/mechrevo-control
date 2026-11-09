import Adw from 'gi://Adw';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Graphene from 'gi://Graphene';
import {CurveEditor, validCurve, DEFAULTS} from '../extension/curveEditor.js';
const sleep = ms => new Promise(r => GLib.timeout_add(GLib.PRIORITY_DEFAULT, ms, () => { r(); return GLib.SOURCE_REMOVE; }));
Adw.init();
Gio.resources_register(Gio.Resource.load('/usr/share/gnome-shell/org.gnome.Shell.Extensions.src.gresource'));
const {default: Preferences} = await import('../extension/prefs.js');
if (typeof Preferences.prototype.fillPreferencesWindow !== 'function') throw new Error('Preferences entry point missing');
let stored = [...DEFAULTS], mode = 'auto', writes = 0, fail = false;
const window = new Adw.PreferencesWindow();
const rpc = async (method, args) => {
    await sleep(10);
    if (fail) throw new Error('模拟连接失败');
    if (method === 'Set') { writes++; stored = args[1].split(' ').map(Number); mode = 'curve'; return []; }
    return [JSON.stringify({vendor: {fan_curve: stored.join(' '), fan_control: mode}})];
};
const editor = new CurveEditor(window, rpc);
window.present();
await sleep(400);
if (!editor.ready || writes) throw new Error('Initial read must not write');
editor.points[0].set_value(70);
if (editor.applyButton.sensitive || validCurve(editor.values())) throw new Error('Descending curve accepted');
await editor.apply();
if (writes) throw new Error('Invalid values dispatched');
editor.defaults.emit('clicked');
editor.points[0].set_value(0);
await editor.apply();
if (writes !== 1 || stored[0] !== 0 || !editor.feedback.label.startsWith('已应用')) throw new Error('Apply/readback failed');
if (!editor.charts.every(chart => chart.sensitive)) throw new Error('Charts must be editable');
editor.points[4].set_value(85);
editor.points[9].set_value(90);
await editor.apply();
if (stored[4] !== 85 || stored[9] !== 90) throw new Error('Last bin not saved');
fail = true;
await editor.apply();
if (!editor.feedback.label.startsWith('应用未确认') || editor.busy) throw new Error('Write error feedback failed');
fail = false;
await editor.load();
if (editor.values()[0] !== 0) throw new Error('Reload failed');
if (editor.charts.length !== 2) throw new Error('Expected separate CPU/GPU charts');
const gpuBefore = editor.values().slice(5).join();
const chart = editor.charts[0];
const g = editor.geometry(chart.get_width(), chart.get_height());
if (!editor.beginDrag(0, g.x(40), g.y(0))) throw new Error('Cannot pick zero-percent node');
editor.updateDrag(0, -1000);
if (editor.values().slice(0, 5).some(value => value !== 100))
    throw new Error('Dragging first node to maximum must raise all later nodes');
editor.updateDrag(0, 1000);
editor.dragPoint = null;
if (editor.values()[0] !== 0 || editor.dragPoint !== null) throw new Error('Cannot drag back to zero');
if (editor.values().slice(5).join() !== gpuBefore) throw new Error('CPU drag changed GPU');
const beforeWrites = writes;
editor.populate([30, 40, 60, 80, 100, 0, 10, 40, 70, 100]);
editor.beginDrag(0, g.x(40), g.y(30));
editor.updateDrag(0, g.y(70) - g.y(30));
if (editor.values().join() !== '70,70,70,80,100,0,10,40,70,100' || !validCurve(editor.values()))
    throw new Error('First node must raise only lower subsequent nodes in the same fan');
const gpuGeometry = editor.geometry(editor.charts[1].get_width(), editor.charts[1].get_height());
editor.beginDrag(1, gpuGeometry.x(50), gpuGeometry.y(10));
editor.updateDrag(1, gpuGeometry.y(60) - gpuGeometry.y(10));
if (editor.values().join() !== '70,70,70,80,100,0,60,60,70,100' || !validCurve(editor.values()))
    throw new Error('GPU middle node must preserve earlier nodes and higher later nodes');
editor.dragPoint = null;
editor.populate([0, 0, 30, 60, 100, 0, 10, 40, 70, 100]);
if (writes !== beforeWrites) throw new Error('Draft changes wrote hardware');
await editor.apply();
await editor.load();
if (editor.values()[0] !== 0 || editor.values()[5] !== 0) throw new Error('Zero curve readback was clamped');
const gpuChart = editor.charts[1];
const gg = editor.geometry(gpuChart.get_width(), gpuChart.get_height());
const cpuBefore = editor.values().slice(0, 5).join();
if (!editor.beginDrag(1, gg.x(80), gg.y(100))) throw new Error('Cannot pick GPU last point');
editor.updateDrag(1, 1000);
editor.dragPoint = null;
if (editor.values()[9] !== 70 || editor.values().slice(0, 5).join() !== cpuBefore)
    throw new Error('GPU drag bounds or channel isolation failed');
if (writes !== beforeWrites + 1) throw new Error('Dragging wrote hardware');
editor.busy = true;
if (editor.beginDrag(0, g.x(40), g.y(0))) throw new Error('Busy editor accepts drag');
editor.busy = false;
editor.populate(DEFAULTS);
// Each node starts the horizontal segment to its right; the incoming segment
// keeps the preceding node's height (zero before the first threshold).
for (const fan of [0, 1]) {
    const paths = [];
    let path = [], width = 0;
    const cr = {
        setLineWidth(value) { width = value; },
        setSourceRGBA() {}, setSourceRGB() {}, showText() {}, arc() {}, fill() {},
        moveTo(x, y) { path = [[x, y]]; },
        lineTo(x, y) { path.push([x, y]); },
        stroke() { if (width === 2.5) paths.push(path); path = []; },
    };
    editor.draw(cr, 640, 160, fan);
    const geometry = editor.geometry(640, 160);
    const values = editor.values().slice(fan * 5, fan * 5 + 5);
    const expected = [[geometry.x(20), geometry.y(0)]];
    for (let i = 0; i < 5; i++) {
        expected.push([geometry.x(40 + i * 10), geometry.y(i ? values[i - 1] : 0)]);
        expected.push([geometry.x(40 + i * 10), geometry.y(values[i])]);
    }
    expected.push([geometry.x(90), geometry.y(values[4])]);
    if (JSON.stringify(paths) !== JSON.stringify([expected]))
        throw new Error('Curve nodes must control outgoing, not incoming segments');
}
const dragFile = GLib.getenv('MECHREVO_CURVE_DRAG');
if (dragFile) {
    const geometry = editor.geometry(chart.get_width(), chart.get_height());
    const [ok, origin] = chart.compute_point(window, new Graphene.Point({x: 0, y: 0}));
    if (!ok) throw new Error('Cannot locate CPU chart');
    GLib.file_set_contents(dragFile, JSON.stringify({x: origin.x + geometry.x(40),
        y: origin.y + geometry.y(30), endY: origin.y + geometry.y(0)}));
    await sleep(4200);
    if (editor.values()[0] !== 0 || writes !== beforeWrites + 1)
        throw new Error(`Real pointer drag failed: value=${editor.values()[0]}, writes=${writes}, expected=${beforeWrites + 1}`);
} else await sleep(1200);
print('PASS: two draggable charts, real pointer to zero, independent channels; load, validation, apply/readback, failure and reload');
window.destroy();
