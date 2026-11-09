#!/bin/sh
# Build versioned artifacts only from a clean, tagged commit. Does not push or install.
set -eu
cd "$(dirname "$0")/.."
version=$(cat VERSION)
test -z "$(git status --porcelain)" || { echo 'Commit all changes before releasing.' >&2; exit 1; }
test "$(git rev-parse HEAD)" = "$(git rev-parse "v$version^{commit}")"
sh scripts/package.sh
release_dir="$PWD/dist/v$version"
mkdir -p "$release_dir"
cp dist/mechrevo-control@local.shell-extension.zip "$release_dir/"
cp "dist/mechrevo-control-driver-$version.tar.gz" "$release_dir/"
git archive --format=tar.gz --prefix="mechrevo-control-$version/" -o "$release_dir/mechrevo-control-$version-source.tar.gz" "v$version"
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT HUP INT TERM
bundle="$staging/mechrevo-control-$version"
mkdir -p "$bundle/dist"
cp -R scripts data "$bundle/"
# Only source-controlled driver files belong in the binary bundle.
mkdir "$bundle/driver"
tar -xzf "dist/mechrevo-control-driver-$version.tar.gz" -C "$bundle/driver"
cp README.md LICENSE VERSION CHANGELOG.md "$bundle/"
cp dist/mechrevo-control dist/mechrevo-control@local.shell-extension.zip "$bundle/dist/"
cp -R docs "$bundle/"
tar -czf "$release_dir/mechrevo-control-$version-linux-$(uname -m).tar.gz" -C "$staging" "mechrevo-control-$version"
(cd "$release_dir" && sha256sum ./*.tar.gz ./*.zip > SHA256SUMS)
printf 'Release artifacts: %s\n' "$release_dir"
