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
