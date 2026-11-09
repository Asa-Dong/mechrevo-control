#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test "$(id -u)" = 0 || { echo 'Run with sudo after building.' >&2; exit 1; }
command -v dkms >/dev/null
test -f dist/mechrevo-control
version=$(cat VERSION)
source_dir="/usr/src/mechrevo-control-$version"
install -d "$source_dir"
install -m644 driver/mechrevo_control.c driver/Makefile driver/dkms.conf driver/COPYING driver/README.md "$source_dir/"
dkms install -m mechrevo-control -v "$version"
# Reload an earlier module only after DKMS installation succeeded.
systemctl stop mechrevo-control-system.service 2>/dev/null || true
if [ -d /sys/module/mechrevo_control ]; then modprobe -r mechrevo_control; fi
modprobe mechrevo_control
found=false
for p in /sys/class/hwmon/hwmon*; do
    if [ "$(cat "$p/name")" = mechrevo ]; then found=true; fi
done
"$found" || { echo 'Driver did not bind; check DMI and WMI driver conflicts.' >&2; exit 1; }
install -Dm755 dist/mechrevo-control /usr/local/libexec/mechrevo-control
install -Dm644 data/io.github.mechrevo.Control1.conf /etc/dbus-1/system.d/io.github.mechrevo.Control1.conf
install -Dm644 data/io.github.mechrevo.control.policy /usr/share/polkit-1/actions/io.github.mechrevo.control.policy
install -Dm644 data/mechrevo-control-system.service /etc/systemd/system/mechrevo-control-system.service
printf '%s\n' mechrevo_control > /etc/modules-load.d/mechrevo-control.conf
busctl --system call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig
systemctl daemon-reload
systemctl enable --now mechrevo-control-system.service
systemctl restart mechrevo-control-system.service
echo 'System service installed. Install/reload the GNOME extension from your normal user session.'
