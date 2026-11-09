# 独显运行时休眠与 NVML 会话复用

环境：RTX 5070 Ti Laptop (`0000:01:00.0`)，NVIDIA open 615.71.09；内屏由 AMD 核显驱动，独显没有活动显示输出。以下仅验证独显 RTD3，不涉及整机 suspend/hibernate。

## 原查询方式的问题

旧实现由扩展约每 5 秒触发一次采样。每轮先检查 PCI runtime_status，若为 active，则重新加载 NVML、调用 nvmlInit_v2、获取设备句柄、读取温度/功耗，最后 nvmlShutdown 并卸载库。

检查 suspended 可以避免主动查询已经休眠的显卡，但不能避免反复初始化/连接设备干扰显卡从 active 进入休眠。本机对照测试中，旧方式在 45 秒观察窗内一直保持 D0；因此原先“不为轮询主动唤醒”的描述不足以说明它对入睡过程的影响。实验验证的是整套会话反复建立/销毁方式，未将影响进一步归因到其中某一个函数。

修复保留同一 NVML 会话和句柄，休眠时不查询，唤醒后复用句柄。实际系统服务持续查询的两轮测试分别约 10.96 秒、21 秒进入 D3cold。无需停用温度/功耗监测或周期性暂停服务。

## 对照实验

临时 SIGSTOP 本项目系统服务，使用 Python ctypes 调用与项目相同的 NVML 温度/功耗接口；实验 finally 恢复服务。两组均每 5 秒读取一次，遇到 sysfs suspended 时跳过 NVML。

- 持续会话：仅初始化、获取句柄一次。第 25 秒观测到 suspended / D3cold，此后第 30、35、40 秒仍休眠，runtime_suspended_time 持续增长。
- 原方式：每个采样批次初始化、获取句柄、查询、shutdown。45 秒观察窗中未进入休眠，累计休眠时间不增长。

持续会话另外通过两轮 on → auto 的唤醒/休眠实验：同一设备句柄跨 D3cold 保留，唤醒后温度/功耗正常返回并变化，再次自动休眠。strace 中 NVIDIA ioctl 集中在初始化与最终清理，采样循环中没有新的 NVIDIA ioctl。这支持复用会话避免重复设备连接的方案；不将这一观察推广为所有 NVML API、驱动版本都无电源副作用。

## 实现

- 服务懒初始化并持有 NVML 库、会话和指定 PCI 设备句柄，正常退出统一 shutdown。
- 仅 sysfs runtime_status=active 时查询 NVML。suspended/transitioning 返回 null 温度/功耗，保留会话，不以旧值充当实时值。
- 初始化失败或 GPU/驱动失效后限频重连（30 秒）；单项指标不支持不反复重建会话。
- 不设置全局 RUSD polling mask，不访问私有 RUSD 内存布局，不修改 NVIDIA 驱动参数。
- 不再声称必须手动读数或周期暂停 20–30 秒才能兼顾遥测和休眠：本机持续会话测试已证明无需该暂停策略。

## 检查与实机复测

`ZIG=/path/to/zig-0.14.1 sh scripts/check.sh` 通过（版本、格式、Shell 检查、Zig 单元测试、ReleaseSafe 构建、生命周期模拟、D-Bus smoke）。新增单元测试覆盖跨采样/休眠保持会话、休眠及切换状态零查询、唤醒后继续采样、指标不支持、GPU 丢失后关闭一次与重试冷却。

本机已更新 `/usr/local/libexec/mechrevo-control` 并重启系统服务，旧程序备份为 `/tmp/mechrevo-control-before-nvml-session`。

实机脚本：

```sh
sudo env MECHREVO_HARDWARE_TEST=1 python3 tests/gpu-runtime-pm.py
```

脚本不暂停监测服务，保留 GNOME 扩展正常刷新，额外约每秒调用一次实际系统 D-Bus GetStatus。通过两轮唤醒有温度/功耗、自动进入 D3cold、连续十秒休眠且返回 null 的断言，最后恢复原 power/control 策略。详见随附 JSONL 记录。

边界：只验证上述机器/驱动下的空闲运行时休眠与唤醒；没有验证整机睡眠、驱动热卸载、其他显卡型号、多独显或长时间压力测试。sysfs 状态检查与 NVML 查询不是原子操作，不能作出任何并发情况下绝不影响电源状态的保证。
