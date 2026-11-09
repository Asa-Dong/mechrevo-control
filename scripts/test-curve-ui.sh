#!/bin/sh
# Isolated GTK editor with stubbed hardware writes and real pointer gestures.
set -eu
project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
output=${1:-"$project_dir/dist/curve-ui"}
mkdir -p "$output"
output=$(CDPATH='' cd -- "$output" && pwd)
timeout 60s dbus-run-session -- env GSETTINGS_BACKEND=memory \
    LIBGL_ALWAYS_SOFTWARE=1 MECHREVO_UI_SCREENSHOT="$output/editor.png" \
    gnome-shell --headless --virtual-monitor 1280x1000 --no-x11 --wayland \
    --wayland-display mechrevo-curve-test --automation-script "$project_dir/tests/curve-editor-shell.js" \
    > "$output/test.log" 2>&1
grep '^PASS:' "$output/test.log"
