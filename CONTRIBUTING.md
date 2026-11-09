# 开发与发布

## 开发环境

固定 Zig 版本见 `.zig-version`。Debian 上检查工具包括 `build-essential libsystemd-dev pkg-config python3 zip dbus gjs shellcheck clang-format`；完整 UI 测试需要 GNOME Shell 48。格式规范见 `.editorconfig` 和 `.clang-format`。

```sh
zig fmt build.zig src
clang-format -i driver/mechrevo_control.c
sh scripts/check.sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD/driver" W=1 modules
sh scripts/test-ui.sh
```

`ZIG=/绝对路径/zig` 可覆盖编译器路径。默认检查不写硬件、不安装驱动；实机测试需要显式设置 `MECHREVO_HARDWARE_TEST=1`，详见 [tests/README.md](tests/README.md)。

## 代码边界

- `src/main.zig` 负责进程和 D-Bus 授权；`control.zig` 校验控制白名单；`status.zig` 组合状态；`telemetry.zig` 采集 RAPL/NVML。
- 驱动只开放验证过的属性，不提供任意 EC 地址访问。
- 不可用的数据保持空值，不用零或默认配置冒充硬件读数。
- 内核生命周期回调不重放/还原。用户态仅在服务启动时恢复 RGB、风扇和充电偏好；手动恢复应用固定预设并保存这三类偏好。
- 修改协议时补上来源、适配范围和实测边界。历史诊断代码不加入产品依赖。

## 发布

`VERSION` 为发行版本唯一来源；GNOME 的整数 `version` 是独立递增的扩展修订号。

1. 修改 `VERSION`，递增 `extension/metadata.json` 的整数 `version`。
2. 执行 `python3 scripts/version.py --write` 同步模块、DKMS 和扩展显示版本。
3. 更新 CHANGELOG、运行检查、内核构建和 UI 验证。
4. 提交所有变更并建立带说明的 `vX.Y.Z` 标签。
5. 执行 `sh scripts/release.sh`，产物在 `dist/vX.Y.Z/`。
6. 验证 `SHA256SUMS`，推送提交与标签，将产物上传到对应 GitHub Release。

发布脚本要求工作区干净且标签指向 HEAD，不安装到本机，也不自动推送。产物包含源码包、Linux 用户态二进制安装包、DKMS 驱动源码包、GNOME 扩展及校验和。二进制包中的驱动必须在目标系统通过 DKMS 编译，不分发预编译内核模块。

## GitHub Actions 模板

`ci/github-actions.yml` 是验证过本地等效步骤的 CI 模板，尚未启用。首次发布使用的 GitHub 凭据没有 `workflow` scope，无法提交自动执行的工作流。具备相应权限后，将模板复制到 `.github/workflows/ci.yml` 并提交即可启用；在此之前以 `scripts/check.sh` 的本地结果为准。
