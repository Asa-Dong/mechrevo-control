# 参考仓库

已克隆到 `pkgbuild-mechrevo-driver-dkms-16-pro/`：

- URL：https://github.com/sklynic/pkgbuild-mechrevo-driver-dkms-16-pro
- 初始固定提交：`af56c84451d6e003267a886183b7deb94ec4476a`
- PKGBUILD 引用：`tuxedo-drivers v4.25.0_rc`
- 用途：机械革命机型匹配、功耗配置、键盘事件与 DKMS 打包参考。

这是独立 Git 克隆，被父项目 `.gitignore` 排除；保留自身提交历史。恢复相同参考版本：

```sh
git clone https://github.com/sklynic/pkgbuild-mechrevo-driver-dkms-16-pro.git references/pkgbuild-mechrevo-driver-dkms-16-pro
git -C references/pkgbuild-mechrevo-driver-dkms-16-pro checkout af56c84451d6e003267a886183b7deb94ec4476a
```

PKGBUILD 面向 Arch，不应直接作为 Debian 安装脚本执行。这个小仓库提供补丁与构建配方；完整驱动源码由其构建配置从上游下载。

新增 EC RAM 曲线协议参考 `Open-Mechrevo-FanSlider/`：

- https://github.com/nemoiwkryang/Open-Mechrevo-FanSlider
- 固定提交：c6665cfc15b7ceb0d08111ea317d9ceccc2feb2e，MIT。
- 仅阅读其协议并在本机受控验证，没有安装或运行 Windows 工具。型号/固件并不完全相同，保留本机 RPM 地址映射。

电池百分比限充协议参考（在线阅读，未克隆或运行）：

- https://github.com/Leo-stro/mechrevo-charge-limit
- 调查日期：2026-11-10；调查时 main HEAD：`75a8061472a2b9ca07b49f35601c21ba4c735c1f`，MIT。
- 阅读 `README.md`、`docs/FINDINGS.md`、`电池充电上限.pyw`；其无界 15X 实测不能直接视为本机兼容证据。
- 协议与当前三档配置的区别、验证边界见 [电池配置调查](../docs/validation/2026-11-09-charge-profile.md#2026-11-10补充百分比限充参考)。

百分比限充的持续门控参考 `open-revo/`（仅克隆、阅读，未运行）：

- https://github.com/faintonce/open-revo
- 固定提交：`b7d2b526e60cd04965eadab21e606bdb1e1aa56e`，GPL-3.0。
- 阅读 `src-tauri/src/driver/ec_bridge.rs`、`hal/driver_impl.rs`、`commands/power_cmd.rs`、`config/ec_registry.json` 及机型配置。
- 三档目标为 100/80/60；旧同方路径还会设置下限为上限减 5，并可持续维持平台值 4。缓存、异步写入返回及寄存器读回均不能代替实际充电验收。
- 本机临时门控与恢复后的实测差异见 [限充调查](../docs/validation/2026-11-10-charge-limit.md)。
