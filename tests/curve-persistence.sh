#!/bin/sh
# Explicit hardware test: settings remain active without a userspace heartbeat.
set -eu
test "${MECHREVO_HARDWARE_TEST:-}" = 1
test "$(id -u)" = 0
systemctl stop mechrevo-control-system.service
for p in /sys/class/hwmon/hwmon*; do
    if [ "$(cat "$p/name")" = mechrevo ]; then hw=$p; break; fi
done
cleanup() { printf release > "$hw/control"; systemctl start mechrevo-control-system.service; }
trap cleanup EXIT INT TERM
printf release > "$hw/control"
test "$(cat "$hw/control")" = firmware
printf acquire > "$hw/control"
printf curve > "$hw/fan_control"
start=$(date +%s)
while [ $(( $(date +%s) - start )) -lt 125 ]; do
    test "$(cat "$hw/control")" = active
    test "$(cat "$hw/fan_control")" = curve
    sleep 5
done
printf release > "$hw/control"
test "$(cat "$hw/control")" = firmware
test "$(cat "$hw/fan_control")" = auto
test "$(cat "$hw/last_error")" = 0
printf 'PASS: curve stays active without heartbeat beyond 120s; explicit release restores\n'
