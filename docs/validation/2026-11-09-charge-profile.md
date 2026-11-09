# 电池配置切换验证

## 实测结果

机器接交流电，电池 100% / Full。使用精确 DMI 限定、固定寄存器的临时模块；未安装完整 TUXEDO 驱动。

1. 直接写充电配置：0x07A6 初值 0x01，目标 0x21。一秒后仍为 0x01，失败；恢复检查通过。
2. 核对 Linux 上游 uniwill_ec_init：通过 0x0741 的 bit0 启用手动控制。
3. 保存并短暂将 0x0741 从 0x80 改为 0x81，再写充电配置。一秒后 0x07A6=0x21，profile=2（stationary），读回成功。
4. 0x07A6 恢复到 0x01；0x0741 恢复到 0x80。临时模块卸载，restore_pending=0、gate_pending=0。
5. 用独立只读模块复查：充电配置 0x01、控制位 0x80、模式 0x0751=0x20、自定义配置 0x0727=0x00，均与先前记录一致。只读模块也已卸载。

日志：`charge-profile-direct-2026-11-09.log` 与 `charge-profile-gated-2026-11-09.log`。

## 结论与限制

- 已验证本机充电配置 high_capacity → stationary → high_capacity 的写入、读回、恢复。
- 0x0741 bit0 是本次测试成功所需的前置条件；只换写入语言不能解决它。
- 不代表充电电流/电压或停止阈值已验证；电池满电无法验证充电过程中何时停止。
- 没有测试 balanced 档、长期保持、拔插电源或重启后行为。
- 未写百分比阈值、风扇曲线、功耗参数或 BIOS 变量。
- 上述为早期诊断记录；当前产品已由 Zig 服务及驱动统一管理控制位、租约和恢复，接入两档配置切换。

Linux 文档还指出充电配置可能使系统报告的状态无法反映真实充电行为；后续应结合电池电流等指标，不能只看 Full/Charging 文本。

来源：https://docs.kernel.org/next/admin-guide/laptops/uniwill-laptop.html

## 产品界面（v6）

提供高容量 / 固定使用两档按钮，通过系统服务下发；选中状态以固件读回为准。更多信息显示当前配置和实际停充／恢复电量待验证的说明。接口不可用或正在写入时禁用按钮，失败显示错误。0.4.0 起取消租约；正常服务退出或恢复设置会回到接管前配置。未新增百分比阈值或开机自动重放；实际充电行为留待日常使用验证。

## 百分比阈值调查

本机 BAT0 没有 charge_control_start_threshold / charge_control_end_threshold。Linux 上游另有 EC 0x07B9 百分比上限协议，但仅为明确支持的机型开放；未发现本机 JIAOLONG Series-X6xR55xK 的匹配证据。上游文档明确警告不兼容机型可能损坏电池，force 也不会开放此能力。因此没有试写该寄存器，也不能将接口缺失等同于硬件绝对不支持。

stationary 表示长期插电使用配置，不是禁用充电。TUXEDO 对其支持机型的说明是降低充电速度并限制到约 80% 实际容量，操作系统仍可能显示 100%；这些数字不能直接认定适用于本机，仍需本机充电实测。

来源：
- https://docs.kernel.org/next/admin-guide/laptops/uniwill-laptop.html
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/platform/x86/uniwill/uniwill-acpi.c
- https://www.tuxedocomputers.com/en/Battery-charging-profiles-inside-the-TUXEDO-Control-Center.tuxedo
