# 本机调查记录

调查日期：2026-11-08～09。

| 项目 | 已确认值 |
| --- | --- |
| DMI 厂商 | MECHREVO |
| 产品 | JIAOLONG Series |
| 主板 | JIAOLONG Series-X6xR55xK |
| CPU | AMD Ryzen 9 9955HX |
| BIOS | N.1.12MRO13 |
| 当前内核 | 6.19.13+deb13-amd64 |
| GNOME | 48.7 |

## 已验证的转速读取

本机 DSDT 的 `\\_SB.INOU.ECRR(offset)` 使用 `MMRW` 读取 `0xFED50000 + offset` 的一个字节。对应 ACPI 设备为 `INOU0000`。`UNIW0001` 是触摸板，不是该接口。

Linux 上游 Uniwill 驱动使用的转速寄存器：

- 主风扇：`0x0464`（高字节）、`0x0465`（低字节）。
- 副风扇：`0x046C`（高字节）、`0x046D`（低字节）。
- RPM = 高字节 × 256 + 低字节。
- EC CPU 温度：`0x043E`。

此前临时只读模块的三次结果：1899/1981、1896/1983、1888/1981 RPM，温度 56°C。测试后模块已卸载。这里记录的是历史实测，不作为运行时数据。主/副风扇的物理位置尚未拆机验证。界面采用参考协议的 CPU/GPU 通道命名：Main（0x0464）对应 CPU，Secondary（0x046c）对应 GPU；Open-Mechrevo-FanSlider 的 FanProtocol.cs 和 MainForm.cs 使用相同映射。本机第一路曲线随 CPU 温度跨档已验证。该命名不表示散热管路完全独立。

上游参考：https://github.com/torvalds/linux/tree/master/drivers/platform/x86/uniwill

## 模式键与驱动

参考仓库的 TUXEDO v4.25.0_rc 驱动处理 `0xB0` 模式键事件，默认发送 Super+Alt+F6，注释说明用于打开 TUXEDO Control Center。不是自动复刻 Windows 三档轮换。

本机物理模式键的真实事件码、Windows 三档的 EC 标志及功耗参数尚未确认。不要把 Linux power-profiles-daemon 的 performance 标签直接解释成机械革命狂暴模式。

## 当前接口缺口

没有 hwmon fan 输入；LED 列表仅包含键盘锁定等指示灯，没有标准键盘背光设备；未见标准充电阈值和平台模式接口。当前初版只报告这些事实。

已有 7.1.8 内核包含 uniwill-laptop 模块，但检查到的 DMI 匹配不包含本机。不要跨内核加载模块，也不要把 force=1 当作完成适配。
