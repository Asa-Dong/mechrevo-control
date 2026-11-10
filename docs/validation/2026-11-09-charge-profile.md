# 电池配置切换验证

后续结论（2026-11-10）：用户在约 65% 时开启工作站模式，仍充到系统显示 100%，未实现目标百分比限充。独立上限协议的本机测试发现，临时平台门控恢复后不会保持激活，即使存储上限读回正确。最新证据和 100 / 80 / 60 的验收要求见 [限充调查](2026-11-10-charge-limit.md)。以下早期记录仅证明三档配置寄存器的切换。

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

## 2026-11-10补充：百分比限充参考

阅读 [Leo-stro/mechrevo-charge-limit](https://github.com/Leo-stro/mechrevo-charge-limit) 的 README、[逆向记录](https://github.com/Leo-stro/mechrevo-charge-limit/blob/75a8061472a2b9ca07b49f35601c21ba4c735c1f/docs/FINDINGS.md)及主程序。调查时 main HEAD 为 `75a8061472a2b9ca07b49f35601c21ba4c735c1f`。以下是参考项目的协议说明与作者实测，不是本机测试结果。

| 寄存器 | 参考项目中的作用 | 与当前实现的关系 |
| --- | --- | --- |
| `0x07A6` bits5:4 | 长效 / 均衡 / 工作站三档 | 当前驱动使用此接口；读回成功不证明某个百分比停充 |
| `0x07B9` bits6:0、bit7 | 百分比上限、已达上限标志 | 当前驱动未使用；上限值需要屏蔽 bit7 后解码 |
| `0x07C3`、`0x0770` | 平台值、ROM ID 首字节；值为 4/5 时满足其固件门控 | 当前未探测；参考程序只临时修改前者，不写 ROM ID |
| `0x087F` | 存储上限 | 作者经 WMI 邮箱读取验证；直接写入未验证 |
| `0x0742` bit2 | 参考机型的限充控制环路激活标志 | 需等固件更新；不能独自证明真实停充 |

参考程序先写 `0x07B9`，再将 `0x07C3` 临时设为 4，保持 2.5 秒并恢复原值，等待 7 秒后检查控制环路及上限读回。按作者对固件的逆向，临时门控使固件把实时上限保存到 `0x087F`；有效存储值可继续维持控制环路。取消时写入上限 0，并再次触发保存；界面选择 100% 也调用取消逻辑。仅关闭门控而留下旧上限，作者曾观察到无法恢复充电。

作者在无界 15X（WUJIE15XA，BIOS N.1.14MRO50）上验证了百分比停充、取消后恢复充电，以及重启和关机拔电后的保持。其记录末尾明确将蛟龙等其他机型列为未验证；电池完全耗尽后的保持也未实测。这里的保持不能等同于已经证明写入 BIOS Flash。

本机已有 `INOU.ECRR/ECRW` 和 WMI 邮箱访问实现，且 `0x0740=0x1A` 与参考机相同。这说明存在可借鉴的访问路径，不能证明固件充电逻辑相同。现有日志没有 `0x0742`、`0x0770`、`0x07C3`、`0x07B9`、`0x087F` 的完整快照，因此目前也无法判断本机是否只是缺少门控条件。

后续验证应先只读采集上述寄存器，并区分现有 ECRR 读取与 WMI 邮箱读取的地址可见性，特别是 `0x087F`；参考项目的 Windows H2RAM 通道在该地址读到 `0xFF`，不能把它直接解释为存储值。读到相似数值仍不足以确认兼容。确认本机固件语义后，受控写入验证还需覆盖平台值恢复、取消时清除上限及存储值、实际充电电流、达到上限停充和低于上限后的恢复行为。

本次仅做源码及文档对照，没有加载诊断模块或写入 EC，也没有将三档按钮改成未经本机验证的 100% / 80% / 60%。上游 Linux 仍区分充电配置与百分比限充，并对阈值接口采用机型限制，参见 [内核说明](https://docs.kernel.org/next/admin-guide/laptops/uniwill-laptop.html#battery-charging-control)。
