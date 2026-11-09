#!/bin/sh
# Portable checks: no root privileges and no hardware writes.
set -eu
cd "$(dirname "$0")/.."
zig=${ZIG:-zig}
python3 scripts/version.py
"$zig" fmt --check build.zig src
if command -v clang-format >/dev/null; then clang-format --dry-run --Werror driver/mechrevo_control.c; fi
shellcheck scripts/*.sh
for script in scripts/*.sh tests/*.sh; do sh -n "$script"; done
"$zig" build test
"$zig" build -Doptimize=ReleaseSafe
./zig-out/bin/mechrevo-control --version
python3 tests/lifecycle-unit.py
sh scripts/test-service.sh
