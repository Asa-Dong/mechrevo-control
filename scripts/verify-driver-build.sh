#!/bin/sh
# Download, verify, patch and compile ONLY. Never install/load modules.
set -eu
project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
verify_dir="$project_dir/dist/driver-validation"
mkdir -p "$verify_dir"
archive="$verify_dir/tuxedo-drivers-v4.25.0_rc.tar.gz"
if ! test -f "$archive"; then
    curl -fL --max-time 120 'https://gitlab.com/tuxedocomputers/development/packages/tuxedo-drivers/-/archive/v4.25.0_rc/tuxedo-drivers-v4.25.0_rc.tar.gz' -o "$archive"
fi
printf '%s  %s\n' c21e6480c8ad2bc9c629f446b150dabbc853c7ca4002611a4a41f802ecc2ba25 "$archive" | sha256sum -c -
# A unique build directory avoids modifying an earlier validation result.
build_dir=$(mktemp -d "$verify_dir/build.XXXXXX")
tar -xzf "$archive" -C "$build_dir"
source_dir="$build_dir/tuxedo-drivers-v4.25.0_rc"
patch -d "$source_dir" -p1 -i "$project_dir/references/pkgbuild-mechrevo-driver-dkms-16-pro/patch.diff"
make -C "/lib/modules/$(uname -r)/build" M="$source_dir" -j4 modules
printf 'Compiled only, no installation: %s\n' "$source_dir"
