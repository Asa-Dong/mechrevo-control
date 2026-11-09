#!/bin/sh
set -eu
project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
python3 scripts/version.py
version=$(cat VERSION)
"${ZIG:-zig}" build -Doptimize=ReleaseSafe
mkdir -p dist
# Rebuild the extension archive from an explicit manifest.
rm -f dist/mechrevo-control@local.shell-extension.zip
(cd extension && zip -q ../dist/mechrevo-control@local.shell-extension.zip metadata.json extension.js prefs.js curveEditor.js)
cp zig-out/bin/mechrevo-control dist/mechrevo-control
cp data/mechrevo-control.service dist/mechrevo-control.service
cp data/mechrevo-control-system.service data/io.github.mechrevo.Control1.conf data/io.github.mechrevo.control.policy dist/
tar -czf "dist/mechrevo-control-driver-$version.tar.gz" -C driver Makefile mechrevo_control.c dkms.conf COPYING README.md
