#!/usr/bin/env python3
"""Explicit integration test for disk persistence and the fixed reset preset."""
import json
import os
from pathlib import Path
import subprocess

assert os.geteuid() == 0 and os.environ.get('MECHREVO_HARDWARE_TEST') == '1'
call = ['busctl', '--system', '--json=short', 'call', 'io.github.mechrevo.Control1', '/io/github/mechrevo/Control1', 'io.github.mechrevo.Control1']
service = 'mechrevo-control-system.service'
preferences = Path('/var/lib/mechrevo-control/preferences.json')
def status():
    return json.loads(json.loads(subprocess.check_output(call + ['GetStatus']))['data'][0])['vendor']
def setv(key, value):
    subprocess.run(call + ['Set', 'ss', key, str(value)], check=True)
def restart():
    subprocess.run(['systemctl', 'restart', service], check=True)
def hardware():
    return next(p for p in Path('/sys/class/hwmon').glob('hwmon*') if (p/'name').read_text().strip() == 'mechrevo')
def apply(v):
    if v['mode'] in ('office', 'balanced', 'turbo'): setv('mode', v['mode'])
    for k in ('keyboard_brightness', 'keyboard_rgb', 'charging_profile'): setv(k, v[k])
    if v['fan_control'] == 'manual':
        for k in ('fan1_percent', 'fan2_percent'): setv(k, v[k])
    elif v['fan_control'] == 'curve': setv('fan_curve', v['fan_curve'])
    else: setv('fan_control', v['fan_control'])
original = status()
try:
    setv('keyboard_rgb', '40 30 20')
    setv('charging_profile', 'stationary')
    setv('fan1_percent', 35)
    setv('fan2_percent', 45)
    saved = json.loads(preferences.read_text())
    assert saved['fan']['percent1'] == '35' and saved['fan']['percent2'] == '45'
    assert 'mode' not in saved and 'keyboard_brightness' not in saved
    subprocess.run(['systemctl', 'stop', service], check=True)
    hw = hardware()
    # Simulate firmware resetting state without updating the saved preferences.
    for k,v in [('fan_control','auto'), ('keyboard_rgb','0 50 50'), ('charging_profile','high_capacity')]:
        (hw/k).write_text(v)
    subprocess.run(['systemctl', 'start', service], check=True)
    v = status()
    assert (v['keyboard_rgb'],v['charging_profile'],v['fan_control'],v['fan1_percent'],v['fan2_percent']) == ('40 30 20','stationary','manual',35,45),v
    print('PASS: RGB, charging and both manual fan targets restored from disk', flush=True)
    curve = '30 40 60 80 100 35 45 65 85 100'
    setv('fan_curve',curve)
    subprocess.run(['systemctl', 'stop', service], check=True)
    (hw/'fan_control').write_text('auto')
    subprocess.run(['systemctl', 'start', service], check=True)
    v=status()
    assert v['fan_control']=='curve' and v['fan_curve']==curve,v
    print('PASS: curve mode and all ten points restored', flush=True)
    setv('mode','turbo')
    assert json.loads(preferences.read_text())['fan']['mode']=='auto'
    setv('keyboard_brightness',4)
    setv('control','release')
    expected={'mode':'office','fan_control':'auto','keyboard_brightness':1,'keyboard_rgb':'50 50 50','charging_profile':'high_capacity'}
    v=status()
    assert all(v[k]==value for k,value in expected.items()),v
    restart()
    v=status()
    assert all(v[k]==value for k,value in expected.items()),v
    print('PASS: fixed default preset and persisted default preferences', flush=True)
finally:
    subprocess.run(['systemctl','start',service],check=True)
    apply(original)
    print('User selection restored and saved:', status(), flush=True)
