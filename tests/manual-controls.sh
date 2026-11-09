#!/bin/sh
# Explicit hardware integration test; always restores the lease snapshot.
set -eu
test "${MECHREVO_HARDWARE_TEST:-}" = 1
test "$(id -u)" = 0
for p in /sys/class/hwmon/hwmon*; do
    if [ "$(cat "$p/name")" = mechrevo ]; then hw=$p; break; fi
done
set_value() { busctl --system call io.github.mechrevo.Control1 /io/github/mechrevo/Control1 io.github.mechrevo.Control1 Set ss "$1" "$2"; }
trap 'set_value control release' EXIT INT TERM
test "$(cat "$hw/control")" = firmware
before=$(cat "$hw/mode" "$hw/keyboard_brightness" "$hw/keyboard_rgb")
if set_value fan1_percent -1 2>/dev/null; then exit 1; fi
if set_value fan2_percent 101 2>/dev/null; then exit 1; fi
test "$(cat "$hw/control")" = firmware
set_value fan1_percent 50
set_value fan2_percent 100
sleep 6
f1=$(cat "$hw/fan1_input"); f2=$(cat "$hw/fan2_input")
printf '50/100: %s/%s RPM\n' "$f1" "$f2"
test "$f1" -gt 1800; test "$f1" -lt 3600; test "$f2" -gt 4000
set_value fan1_percent 100
set_value fan2_percent 50
sleep 6
f1=$(cat "$hw/fan1_input"); f2=$(cat "$hw/fan2_input")
printf '100/50: %s/%s RPM\n' "$f1" "$f2"
test "$f1" -gt 4000; test "$f2" -gt 1800; test "$f2" -lt 3600
set_value fan1_percent 30
set_value fan2_percent 30
sleep 6
f1=$(cat "$hw/fan1_input"); f2=$(cat "$hw/fan2_input")
printf '30/30: %s/%s RPM\n' "$f1" "$f2"
test "$f1" -gt 1000; test "$f2" -gt 1000
set_value fan_control boost
test "$(cat "$hw/fan_control")" = boost
set_value fan_control auto
test "$(cat "$hw/fan_control")" = auto
set_value keyboard_rgb '0 0 0'
test "$(cat "$hw/keyboard_rgb")" = '0 0 0'
set_value keyboard_rgb '20 30 40'
test "$(cat "$hw/keyboard_rgb")" = '20 30 40'
set_value keyboard_brightness 2
test "$(cat "$hw/keyboard_brightness")" = 2
set_value control release
test "$(cat "$hw/control")" = firmware
after=$(cat "$hw/mode" "$hw/keyboard_brightness" "$hw/keyboard_rgb")
test "$before" = "$after"
test "$(cat "$hw/last_error")" = 0
echo 'PASS: independent sustained fan speeds, transitions, RGB black, brightness, snapshot restoration'
