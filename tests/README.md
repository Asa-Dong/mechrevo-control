# 测试分层

## 默认检查（不写硬件）

`sh scripts/check.sh` 检查版本一致性、Zig/C 格式、Shell 脚本、Zig 单元测试、模拟 EC 生命周期回调及用户会话 D-Bus 的读取/参数/权限边界。

`lifecycle-unit.py` 将实际驱动回调与模拟 EC 编译成可执行程序，验证生命周期零写入和遗留状态重新接管。它不能替代实际睡眠/断电验证。

## UI（隔离桌面，写入 stub）

`sh scripts/test-ui.sh` 启动独立 headless GNOME Shell，执行 `menu-ui.js`，检查按钮、滑条、数据缺失、通道映射及布局。日志和截图默认写入 `dist/ui/`。真实硬件写入被记录器替代，不替换用户桌面的 Shell。

`curve-editor-ui.js` / `curve-editor-shell.js` 分别覆盖 GTK 曲线编辑器和 Shell 交互；需要 GNOME 48 与显示环境。

## 显式实机测试

这些脚本会改变硬件或重载驱动，不纳入 CI。先阅读脚本的前置状态和清理逻辑：

```sh
sudo env MECHREVO_HARDWARE_TEST=1 python3 tests/gpu-runtime-pm.py
sudo env MECHREVO_HARDWARE_TEST=1 python3 tests/lifecycle-hardware.py
sudo env MECHREVO_HARDWARE_TEST=1 sh tests/manual-controls.sh
sudo env MECHREVO_HARDWARE_TEST=1 sh tests/curve-persistence.sh
```

`system-smoke.sh` 要求没有现存系统服务/策略文件；`kernel-smoke.sh` 会卸载模块。其余脚本用于定向曲线、零值、模式切换实验，有的依赖先编译诊断模块。多数早期脚本要求 `control=firmware`，不应批量无差别运行。

历史实测在 `docs/validation/`；描述旧版本的“退出恢复”等语义不适用于 1.0.0。每次验证需区分服务重启、驱动重载、实际睡眠和整机重启。

`gpu-runtime-pm.py` 要求当前系统服务已更新、独显空闲且无活动显示输出。临时将 NVIDIA PCI 电源策略设为 `on` 再设为 `auto`，每秒查询 D-Bus，验证两轮唤醒有读数、自动进入 D3cold、休眠持续十秒且读数为 null；最后恢复原策略。不执行整机休眠，不停止桌面。
