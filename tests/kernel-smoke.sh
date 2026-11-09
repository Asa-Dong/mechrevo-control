#!/bin/sh
# Direct kernel boundary tests, bypassing Zig validation. Ends with module unloaded.
set -eu
cd "$(dirname "$0")/.."
test "$(id -u)" = 0
test "${MECHREVO_HARDWARE_TEST:-}" = 1
hw=
for dir in /sys/class/hwmon/hwmon*; do
    if [ "$(cat "$dir/name")" = mechrevo ]; then hw=$dir; fi
done
test -n "$hw"
test "$(cat "$hw/control")" = firmware
original=$(cat "$hw/mode")
case "$original" in office) raw=0xa0;; balanced) raw=0x20;; turbo) raw=0x30;; *) exit 1;; esac
cleanup() {
    if [ -e "$hw/control" ]; then printf release > "$hw/control" || true; fi
}
trap cleanup EXIT INT TERM
if (printf balanced > "$hw/mode") 2>/dev/null; then exit 1; fi
if (printf keepalive > "$hw/control") 2>/dev/null; then exit 1; fi
test "$(cat "$hw/control")" = firmware
printf acquire > "$hw/control"
if (printf 5 > "$hw/keyboard_brightness") 2>/dev/null; then exit 1; fi
if (printf '51 0 0' > "$hw/keyboard_rgb") 2>/dev/null; then exit 1; fi
if (printf balanced > "$hw/charging_profile") 2>/dev/null; then exit 1; fi
if (printf arbitrary > "$hw/mode") 2>/dev/null; then exit 1; fi
if runuser -u nobody -- sh -c 'printf office > "$1"' sh "$hw/mode" 2>/dev/null; then exit 1; fi
printf balanced > "$hw/mode"
rmmod mechrevo_control
insmod diagnostics/ec-capabilities/mechrevo_probe.ko
rmmod mechrevo_probe
dmesg | tail -25 > docs/validation/kernel-unload-2026-11-09.log
last_mode=$(sed -n 's/.*mode offset=0x0751 value=\(0x[0-9a-f]*\).*/\1/p' docs/validation/kernel-unload-2026-11-09.log | tail -1)
last_gate=$(sed -n 's/.*manual_mode offset=0x0741 value=\(0x[0-9a-f]*\).*/\1/p' docs/validation/kernel-unload-2026-11-09.log | tail -1)
test "$last_mode" = 0x20
test "$last_gate" = 0x81
echo 'PASS: kernel validation, inactive write rejection, root-only sysfs, unload preservation'
