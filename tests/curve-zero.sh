#!/bin/sh
set -eu
test "${MECHREVO_HARDWARE_TEST:-}" = 1
test "$(id -u)" = 0
for p in /sys/class/hwmon/hwmon*; do if [ "$(cat "$p/name")" = mechrevo ]; then hw=$p; break; fi; done
set_value() { busctl --system call io.github.mechrevo.Control1 /io/github/mechrevo/Control1 io.github.mechrevo.Control1 Set ss "$1" "$2"; }
test "$(cat "$hw/control")" = firmware
test "$(cat "$hw/temp1_input")" -lt 60000
saved=$(cat "$hw/fan_curve")
cleanup() { set_value fan_curve "$saved"; set_value control release; }
trap cleanup EXIT INT TERM
set_value fan_curve '0 0 0 80 100 0 0 0 80 100'
test "$(cat "$hw/fan_curve")" = '0 0 0 80 100 0 0 0 80 100'
for i in 1 2 3 4 5 6 7 8 9 10; do
    sleep 3
    printf 'curve 0%% low bins: RPM=%s/%s CPU=%s\n' "$(cat "$hw/fan1_input")" "$(cat "$hw/fan2_input")" "$(cat "$hw/temp1_input")"
    test "$(cat "$hw/temp1_input")" -lt 65000
done
test "$(cat "$hw/last_error")" = 0
echo 'PASS: zero curve values accepted; actual RPM above; original configuration will be restored'
