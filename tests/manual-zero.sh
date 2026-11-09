#!/bin/sh
set -eu
test "${MECHREVO_HARDWARE_TEST:-}" = 1
test "$(id -u)" = 0
for p in /sys/class/hwmon/hwmon*; do if [ "$(cat "$p/name")" = mechrevo ]; then hw=$p; break; fi; done
set_value() { busctl --system call io.github.mechrevo.Control1 /io/github/mechrevo/Control1 io.github.mechrevo.Control1 Set ss "$1" "$2"; }
test "$(cat "$hw/control")" = firmware
test "$(cat "$hw/temp1_input")" -lt 65000
trap 'set_value control release' EXIT INT TERM
set_value fan1_percent 0
set_value fan2_percent 0
test "$(cat "$hw/fan1_percent")" = 0
test "$(cat "$hw/fan2_percent")" = 0
for i in 1 2 3 4; do
    sleep 3
    printf '0/0 command: RPM=%s/%s CPU=%s\n' "$(cat "$hw/fan1_input")" "$(cat "$hw/fan2_input")" "$(cat "$hw/temp1_input")"
    test "$(cat "$hw/temp1_input")" -lt 70000
done
set_value control release
test "$(cat "$hw/control")" = firmware
test "$(cat "$hw/last_error")" = 0
echo 'PASS: independent 0% commands accepted, actual RPM recorded, restored'
