#!/bin/sh
# Isolated compositor; test modules stub hardware writes.
set -eu
project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
output=${1:-"$project_dir/dist/ui"}
mkdir -p "$output"
output=$(CDPATH='' cd -- "$output" && pwd)
timeout 60s dbus-run-session -- env GSETTINGS_BACKEND=memory \
    LIBGL_ALWAYS_SOFTWARE=1 MECHREVO_UI_SCREENSHOT="$output/menu.png" \
    gnome-shell --headless --virtual-monitor 1280x900 --no-x11 --wayland \
    --wayland-display mechrevo-ui-test --automation-script "$project_dir/tests/menu-ui.js" \
    > "$output/test.log" 2>&1
grep '^PASS:' "$output/test.log"
