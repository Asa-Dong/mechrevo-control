#!/bin/sh
set -eu
test "${MECHREVO_HARDWARE_TEST:-}" = 1
test "$(id -u)" = 0
for p in /sys/class/hwmon/hwmon*; do if [ "$(cat "$p/name")" = mechrevo ]; then hw=$p; break; fi; done
set_value() {
    started=$(date +%s%N)
    busctl --system call io.github.mechrevo.Control1 /io/github/mechrevo/Control1 io.github.mechrevo.Control1 Set ss "$1" "$2"
    printf '%s=%s elapsed %s ms\n' "$1" "$2" "$(( ($(date +%s%N) - started) / 1000000 ))"
}
trap 'set_value control release; systemctl start mechrevo-control-system.service' EXIT INT TERM
test "$(cat "$hw/control")" = firmware
set_value fan_curve '40 40 40 40 100 60 60 60 60 100'
test "$(cat "$hw/fan_control")" = curve
sleep 8
printf 'Curve RPM=%s/%s CPU=%s\n' "$(cat "$hw/fan1_input")" "$(cat "$hw/fan2_input")" "$(cat "$hw/temp1_input")"
set_value fan_control auto
test "$(cat "$hw/fan_control")" = auto
set_value fan_control curve
test "$(cat "$hw/fan_control")" = curve
set_value fan_control curve
set_value control release
test "$(cat "$hw/control")" = firmware
test "$(cat "$hw/last_error")" = 0
set_value fan_control curve
systemctl stop mechrevo-control-system.service
test "$(cat "$hw/control")" = active
test "$(cat "$hw/fan_control")" = curve
systemctl start mechrevo-control-system.service
echo 'PASS: curve apply, auto, reapply, no-op, release and service stop without table rollback'
