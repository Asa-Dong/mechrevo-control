#!/bin/sh
set -eu
project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
# Run package.sh first. Explicit installation only, no root privileges.
test -f "$project_dir/dist/mechrevo-control"
install -Dm755 "$project_dir/dist/mechrevo-control" "$HOME/.local/libexec/mechrevo-control"
install -Dm644 "$project_dir/data/mechrevo-control.service" "$HOME/.config/systemd/user/mechrevo-control.service"
systemctl --user daemon-reload
systemctl --user enable --now mechrevo-control.service
systemctl --user restart mechrevo-control.service
gnome-extensions install --force "$project_dir/dist/mechrevo-control@local.shell-extension.zip"
echo '服务已启动。首次安装扩展后可能需要注销登录，再执行 gnome-extensions enable mechrevo-control@local。'
