#!/usr/bin/env python3
"""Opt-in RTD3 test: wake the NVIDIA GPU, poll the running service, restore policy.

Requires an idle GPU with no attached active display. Does not suspend the system.
Runs two wake/sleep cycles with one-second D-Bus polling and verifies null telemetry
in D3cold. Run with sudo env MECHREVO_HARDWARE_TEST=1 python3 ...
"""
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    if os.geteuid() != 0 or os.environ.get('MECHREVO_HARDWARE_TEST') != '1':
        raise SystemExit('Requires root and MECHREVO_HARDWARE_TEST=1')
    pci = Path('/sys/bus/pci/devices')
    device = next(p for p in sorted(pci.iterdir())
                  if (p / 'vendor').read_text().strip() == '0x10de'
                  and (p / 'class').read_text().startswith('0x03'))
    control = device / 'power/control'
    original = control.read_text()
    command = ['busctl', '--system', '--json=short', 'call',
               'io.github.mechrevo.Control1', '/io/github/mechrevo/Control1',
               'io.github.mechrevo.Control1', 'GetStatus']

    def status():
        raw = subprocess.check_output(command, timeout=10)
        return json.loads(json.loads(raw)['data'][0])['gpu']

    def hardware():
        return {key: (device / key).read_text().strip() for key in
                ('power/runtime_status', 'power/runtime_suspended_time', 'power_state')}

    def emit(**values):
        print(json.dumps(values), flush=True)

    try:
        for cycle in range(2):
            control.write_text('on\n')
            awake = status()
            emit(cycle=cycle, event='wake', gpu=awake, hardware=hardware())
            assert awake['state'] == 'active', awake
            assert awake['watts'] is not None and awake['celsius'] is not None, awake
            control.write_text('auto\n')
            start = time.monotonic()
            sleeping_since = None
            slept_ms = None
            while time.monotonic() - start < 75:
                before = hardware()
                gpu = status()
                after = hardware()
                emit(cycle=cycle, seconds=round(time.monotonic() - start, 2),
                     gpu=gpu, before=before, after=after)
                asleep = (before['power/runtime_status'] == 'suspended'
                          and after['power/runtime_status'] == 'suspended'
                          and after['power_state'] == 'D3cold')
                if asleep:
                    assert gpu['state'] == 'suspended', gpu
                    assert gpu['watts'] is None and gpu['celsius'] is None, gpu
                    if sleeping_since is None:
                        sleeping_since = time.monotonic()
                        slept_ms = int(after['power/runtime_suspended_time'])
                    if time.monotonic() - sleeping_since >= 10:
                        assert int(after['power/runtime_suspended_time']) - slept_ms >= 9000
                        break
                else:
                    sleeping_since = None
                time.sleep(1)
            else:
                raise AssertionError('GPU did not sustain D3cold for 10 seconds while polling')
            emit(cycle=cycle, result='PASS')
    finally:
        control.write_text(original)
        emit(event='policy_restored', control=control.read_text().strip(), hardware=hardware())


if __name__ == '__main__':
    main()
