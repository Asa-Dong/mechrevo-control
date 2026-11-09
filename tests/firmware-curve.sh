#!/bin/sh
set -eu
test "${MECHREVO_HARDWARE_TEST:-}" = 1
test "$(id -u)" = 0
for p in /sys/class/hwmon/hwmon*; do if [ "$(cat "$p/name")" = mechrevo ]; then hw=$p; break; fi; done
set_value() {
    started=$(date +%s%N)
    busctl --system call io.github.mechrevo.Control1 /io/github/mechrevo/Control1 io.github.mechrevo.Control1 Set ss "$1" "$2"
    result=$?
    printf 'Set %s elapsed: %s ms\n' "$1" "$(( ($(date +%s%N) - started) / 1000000 ))"
    return "$result"
}
stopped=
cleanup() {
    if [ -n "$stopped" ]; then kill -CONT "$stopped"; fi
    set_value control release
}
trap cleanup EXIT INT TERM
test "$(cat "$hw/control")" = firmware
before=$(cat "$hw/mode" "$hw/keyboard_brightness" "$hw/keyboard_rgb")
snapshot() {
    insmod diagnostics/ec-transport/mechrevo_transport_test.ko
    rmmod mechrevo_transport_test
    journalctl -k -n 25 --no-pager -o cat | grep -E 'mechrevo_transport_test: (TABLE|0726|0727|07c5|07c6|0751)' | tail -11
}
snapshot > docs/validation/curve-before.txt
if set_value fan_curve '40 30 60 80 100 30 40 60 80 100' 2>/dev/null; then exit 1; fi
test "$(cat "$hw/control")" = firmware
set_value fan_curve '50 50 50 50 100 80 80 80 80 100'
test "$(cat "$hw/fan_control")" = curve
stopped=$(systemctl show -p MainPID --value mechrevo-control-system.service)
kill -STOP "$stopped"
for i in 1 2 3 4 5 6 7 8 9 10; do
    sleep 5
    printf 'Zig stopped: mode=%s RPM=%s/%s temp=%s\n' "$(cat "$hw/fan_control")" "$(cat "$hw/fan1_input")" "$(cat "$hw/fan2_input")" "$(cat "$hw/temp1_input")"
done
f1=$(cat "$hw/fan1_input"); f2=$(cat "$hw/fan2_input")
test "$f1" -gt 2300; test "$f1" -lt 3400; test "$f2" -gt 3700
kill -CONT "$stopped"; stopped=
set_value fan_curve '50 50 50 50 100 80 80 80 80 100'
set_value fan_control manual
test "$(cat "$hw/fan_control")" = manual
set_value fan_control curve
set_value mode balanced
test "$(cat "$hw/fan_control")" = curve
test "$(cat "$hw/mode")" = balanced
set_value control release
after=$(cat "$hw/mode" "$hw/keyboard_brightness" "$hw/keyboard_rgb")
test "$before" = "$after"
snapshot > docs/validation/curve-after.txt
sed '/TABLE/d' docs/validation/curve-before.txt > /tmp/mechrevo-curve-flags-before.txt
sed '/TABLE/d' docs/validation/curve-after.txt > /tmp/mechrevo-curve-flags-after.txt
cmp /tmp/mechrevo-curve-flags-before.txt /tmp/mechrevo-curve-flags-after.txt
set_value fan_control curve
systemctl stop mechrevo-control-system.service
test "$(cat "$hw/control")" = active
test "$(cat "$hw/last_error")" = 0
systemctl start mechrevo-control-system.service
echo 'PASS: EC curve runs with Zig stopped, independent targets, mode transitions, control flag restoration (table intentionally retained), service stop preservation'
