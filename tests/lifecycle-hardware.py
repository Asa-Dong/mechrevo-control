#!/usr/bin/env python3
"""Explicit hardware test: preserve current preferences across service/module reload."""
import json
import os
import subprocess

assert os.geteuid() == 0 and os.environ.get('MECHREVO_HARDWARE_TEST') == '1'
service = 'mechrevo-control-system.service'
call = ['busctl', '--system', '--json=short', 'call', 'io.github.mechrevo.Control1', '/io/github/mechrevo/Control1', 'io.github.mechrevo.Control1']
def run(*args): subprocess.run(args, check=True)
def snapshot(): return json.loads(subprocess.check_output(['/usr/local/libexec/mechrevo-control', '--once']))['vendor']
def set_value(k, v): run(*call, 'Set', 'ss', k, str(v))
def apply(v):
    if v['mode'] in ('office', 'balanced', 'turbo'): set_value('mode', v['mode'])
    for key in ('keyboard_brightness', 'keyboard_rgb', 'charging_profile'): set_value(key, v[key])
    if v['fan_control'] == 'curve': set_value('fan_curve', v['fan_curve'])
    elif v['fan_control'] == 'manual':
        for key in ('fan1_percent', 'fan2_percent'): set_value(key, v[key])
    else: set_value('fan_control', v['fan_control'])

original = snapshot()
keys = ('mode', 'keyboard_brightness', 'keyboard_rgb', 'charging_profile', 'fan_control', 'fan_curve')
try:
    # Deliberately change charge selection so retention cannot pass by matching the baseline.
    set_value('charging_profile', 'high_capacity' if original['charging_profile'] == 'stationary' else 'stationary')
    before = snapshot()
    run('systemctl', 'stop', service)
    after = snapshot()
    assert all(before[k] == after[k] for k in keys), (before, after)
    assert after['control'] == 'active', after
    print('PASS: service stop preserves active settings', flush=True)
    run('modprobe', '-r', 'mechrevo_control')
    run('modprobe', 'mechrevo_control')
    after = snapshot()
    assert all(before[k] == after[k] for k in keys), (before, after)
    assert after['control'] == 'retained', after
    print('PASS: driver reload reads retained EC settings', flush=True)
    run('systemctl', 'start', service)
    set_value('charging_profile', before['charging_profile'])
    assert snapshot()['control'] == 'active'
    set_value('control', 'release')
    after = snapshot()
    assert after['control'] == 'active' and after['fan_control'] == 'auto' and after['mode'] == 'office', after
    print('PASS: reacquire and explicit reset remain available', flush=True)
finally:
    run('modprobe', 'mechrevo_control')
    run('systemctl', 'start', service)
    apply(original)
    print('Restored user preferences:', snapshot(), flush=True)
