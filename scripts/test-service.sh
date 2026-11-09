#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
# The nested shell evaluates these expressions.
# shellcheck disable=SC2016
dbus-run-session -- sh -eu -c '
./zig-out/bin/mechrevo-control &
service_pid=$!
trap '\''kill "$service_pid" 2>/dev/null || true; wait "$service_pid" 2>/dev/null || true'\'' EXIT INT TERM
attempt=0
until busctl --user call io.github.mechrevo.Control1 /io/github/mechrevo/Control1 org.freedesktop.DBus.Introspectable Introspect >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    test "$attempt" -lt 30
    sleep 0.1
done
gjs -m tests/dbus-smoke.js
ps -o rss=,comm= -p "$service_pid"
'
