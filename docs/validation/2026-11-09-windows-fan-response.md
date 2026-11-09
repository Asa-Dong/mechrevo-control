# Windows 风扇响应速度静态分析

Windows 分区 `/dev/nvme0n1p2` 以 `ro,nosuid,nodev,noexec` 挂载于 `/mnt/mechrevo-windows`。未执行 Windows 程序，未写入 EC。

界面包：`ControlCenter3_5.56.51.49_x64__wrbgcf7aesyd8`。后台：`Program Files/OEM/机械革命控制中心/AiStoneService/MyControlCenter/GCUService.exe`。

## 已取得的证据

- 界面字符串 `strFanSwitchSpeed` / `strFanSwitchSpeedEnabled`，原文 `Fan Switch Speed` / `Enable Fan Switch Speed`。
- .NET Constant 表中 `RamFan1p5_ECSpec.ADDR_TimAP_FanSwitchSpeedT100mSec` 的原始字节为 `87 07`，小端 UInt16 值为 `0x0787`。这是程序中明确存在的地址常量，尚未证明本机具体执行路径使用它。
- 同类包含 CPU/GPU 曲线地址 `0x0F00/10/20`、`0x0F30/40/50`，与现有项目使用的表地址一致。
- 该类还定义 `ADDR_RAMFAN1P5_TABLE_STATUS1=0x0F5D`、`STATUS2=0x0F5E`、`CTRL=0x0F5F`。这是额外线索，不能直接据此修改现有表协议；当前项目仍把这些位置作为尾段 duty，需后续确认具体机型的使用方式。
- 多个 UserPofiles JSON 保存 `FanSwitchSpeedEnabled: 0`、`FanSwitchSpeed: 300`。这些是保存值，不足以认定范围、单位或默认值。
- 找到 `SetFanSwitchSpeed` 的 Normal、NV、CML 等实现名称，以及基类 `SetFanSwitchSpeedEnabled`。它们的 MethodDef RVA 均指向 `0x8095E0`，普通 IL 内容仅构造并抛出 `Runtime exception`，无法直接还原 EC 写入逻辑。
- 扫描可解析的其他 IL 方法，未找到可用的该参数设置调用链。

## 结论和未确认项

符号名提示底层时间单位为 100 ms，但不能据此认定保存值 300 的转换方式。仍需确认开关位、合法范围、升速/降速是否共用、与停转等待计时是否相关，以及本机使用的具体后端。

目前只能把 `0x0787` 作为强候选，不能凭符号名直接写入。下一步可在 Windows 修改响应速度选项，比较相关 EC 读回和配置，或取得保留真实方法体的安装包继续静态分析。没有把该功能添加到 Linux 控制界面。

分析脚本与输出位于 `dist/windows-analysis/`，未复制或提交完整 Windows 二进制。

GCUService.exe SHA-256：`0469cf6d9f55b9c9abed70db3a11ad126f5bad2e9246f45fb747e17e11aed450`。

## 用户补充 Windows 截图

截图显示“启用风扇转换灵敏度”已勾选；最小 100、最大 12700、当前 11600，单位均为 `ms/0.5%`。因此低 7 位保存 100 ms 倍数（1–127）、bit7 作为启用位是有依据但尚未实测确认的编码假设：当前值可能对应 0xF4，而 300/600 ms 的启用状态可能对应 0x83/0x86。之前测试的 0x03/0x06 若未带启用位，可能不会改变调速行为。

收到截图后的 Linux 只读检查：0x0787=0，系统服务 inactive，control=firmware、fan_control=auto，原有自定义曲线读回 ENODATA。这不是 Windows 选项生效时的同步读数，不能据此否定截图，也不能认定最高位就是开关。本轮未写 EC，未启动服务或改变当前自动模式。

后续实机验证已支持最高位启用编码：0x83 约 300 ms/0.5%，0x86 约 600 ms/0.5%。详见 `2026-11-09-fan-response-hardware.md`。此前“单独修改无效果”的结论限于未设置最高位的 0x03/0x06。
