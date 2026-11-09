#!/bin/sh
# Explicit on-device test; temporary bus policy and daemon are removed on exit.
set -eu
cd "$(dirname "$0")/.."
test "$(id -u)" = 0
test "${MECHREVO_HARDWARE_TEST:-}" = 1
name=io.github.mechrevo.Control1
object=/io/github/mechrevo/Control1
policy=/etc/dbus-1/system.d/io.github.mechrevo.Control1-test.conf
authpolicy=/usr/share/polkit-1/actions/io.github.mechrevo.control.policy
test ! -e "$policy"
test ! -e "$authpolicy"
if busctl --system status "$name" >/dev/null 2>&1; then
    echo 'An existing system service is running; refusing to interfere.' >&2
    exit 1
fi
hw=
for dir in /sys/class/hwmon/hwmon*; do
    if [ "$(cat "$dir/name")" = mechrevo ]; then hw=$dir; fi
done
test -n "$hw"
test "$(cat "$hw/control")" = firmware
before=$(cat "$hw/mode" "$hw/fan_control" "$hw/keyboard_brightness" "$hw/keyboard_rgb" "$hw/charging_profile")
pid=
cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    printf release > "$hw/control" || true
    rm -f "$policy" "$authpolicy"
    busctl --system call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig >/dev/null
}
trap cleanup EXIT INT TERM
install -m644 data/io.github.mechrevo.Control1.conf "$policy"
install -m644 data/io.github.mechrevo.control.policy "$authpolicy"
pkaction --action-id io.github.mechrevo.control.manage >/dev/null
busctl --system call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig
./zig-out/bin/mechrevo-control --system &
pid=$!
tries=0
until busctl --system call "$name" "$object" "$name" GetStatus >/dev/null 2>&1; do
    tries=$((tries + 1)); test "$tries" -lt 30; sleep 0.1
done
set_value() { busctl --system call "$name" "$object" "$name" Set ss "$1" "$2"; }
if set_value keyboard_brightness 5 >/dev/null 2>&1; then exit 1; fi
test "$(cat "$hw/control")" = firmware
if runuser -u nobody -- busctl --system call "$name" "$object" "$name" Set ss mode balanced >/dev/null 2>&1; then
    echo 'Unauthorized write accepted' >&2; exit 1
fi
test "$(cat "$hw/control")" = firmware
runuser -u nobody -- busctl --system call "$name" "$object" "$name" GetStatus >/dev/null
set_value keyboard_brightness "$(cat "$hw/keyboard_brightness")"
set_value keyboard_rgb "$(cat "$hw/keyboard_rgb")"
set_value charging_profile "$(cat "$hw/charging_profile")"
set_value fan_control auto
set_value mode balanced
test "$(cat "$hw/control")" = active
test "$(cat "$hw/mode")" = balanced
set_value control release
test "$(cat "$hw/control")" = firmware
after=$(cat "$hw/mode" "$hw/fan_control" "$hw/keyboard_brightness" "$hw/keyboard_rgb" "$hw/charging_profile")
test "$before" = "$after"
# SIGTERM must leave the active settings in EC.
set_value mode balanced
kill "$pid"
wait "$pid"
pid=
test "$(cat "$hw/control")" = active
test "$(cat "$hw/mode")" = balanced
printf release > "$hw/control"
after=$(cat "$hw/mode" "$hw/fan_control" "$hw/keyboard_brightness" "$hw/keyboard_rgb" "$hw/charging_profile")
test "$before" = "$after"
echo 'PASS: system D-Bus reads, rejection, controls, explicit release and SIGTERM preservation'
