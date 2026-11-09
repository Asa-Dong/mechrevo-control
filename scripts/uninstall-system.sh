#!/bin/sh
set -eu
test "$(id -u)" = 0
systemctl disable --now mechrevo-control-system.service
if [ -d /sys/module/mechrevo_control ]; then modprobe -r mechrevo_control; fi
# Discover installed project versions instead of maintaining a release list.
versions=$(dkms status -m mechrevo-control | sed -n 's@^mechrevo-control/\([^,]*\),.*@\1@p' | sort -u)
for version in $versions; do
    dkms remove -m mechrevo-control -v "$version" --all
done
rm -f /etc/modules-load.d/mechrevo-control.conf /etc/systemd/system/mechrevo-control-system.service
rm -f /etc/dbus-1/system.d/io.github.mechrevo.Control1.conf /usr/share/polkit-1/actions/io.github.mechrevo.control.policy
rm -f /usr/local/libexec/mechrevo-control
systemctl daemon-reload
busctl --system call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig
