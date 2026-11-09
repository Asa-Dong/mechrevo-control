#!/usr/bin/env python3
"""Hardware opt-in: verify curve PWM while retaining each performance mode."""
import pathlib, time, os, subprocess
assert os.geteuid() == 0 and os.environ.get('MECHREVO_HARDWARE_TEST') == '1'
p = next((p for p in pathlib.Path('/sys/class/hwmon').glob('hwmon*') if (p / 'name').read_text().strip() == 'mechrevo'))
s = {k: (p / k).read_text().strip() for k in ['mode', 'fan_control', 'fan_curve', 'fan1_percent', 'fan2_percent']}
service = 'mechrevo-control-system.service'
was_active = subprocess.run(['systemctl', 'is-active', '--quiet', service]).returncode == 0
subprocess.run(['systemctl', 'stop', service], check=True)

def read(k):
    return (p / k).read_text().strip()

def write(k, v):
    (p / k).write_text(str(v))
try:
    write('control', 'acquire')
    write('mode', 'office')
    write('fan_curve', ' '.join(['80'] * 10))
    for mode, target in [('office', 80), ('balanced', 80), ('turbo', 80), ('turbo', 40)]:
        write('mode', mode)
        write('fan_curve', ' '.join([str(target)] * 10))
        reached = False
        for i in range(12):
            time.sleep(5)
            row = {k: read(k) for k in ['mode', 'fan_control', 'fan1_percent', 'fan2_percent', 'fan1_input', 'fan2_input', 'temp1_input', 'last_error']}
            print('target', target, row, flush=True)
            assert row['mode'] == mode and row['fan_control'] == 'curve' and (row['last_error'] == '0')
            assert int(row['temp1_input']) < 75000
            if all((abs(int(row[k]) - target) <= 1 for k in ['fan1_percent', 'fan2_percent'])):
                reached = True
                break
        assert reached, (mode, target, 'PWM failed to converge')
    print('PASS: all three modes coexist with actual curve PWM; upward/downward convergence', flush=True)
finally:
    try:
        write('fan_curve', s['fan_curve'])
        if s['fan_control'] == 'manual':
            write('fan1_percent', s['fan1_percent'])
            write('fan2_percent', s['fan2_percent'])
        write('fan_control', s['fan_control'])
        write('mode', s['mode'])
    finally:
        if was_active:
            subprocess.run(['systemctl', 'start', service], check=True)
