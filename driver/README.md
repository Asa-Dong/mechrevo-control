# 内核驱动

目标仅为 MECHREVO / JIAOLONG Series-X6xR55xK、barebone 0x1A，采用 Linux WMI 驱动绑定 ABBC0F72-8EA1-11D1-00A0-C90629100000，经 ACPI `\_SB_.INOU.ECRR/ECRW` 访问已验证寄存器。风扇即时控制使用 WMI BC method 4，经 EC 邮箱协议执行命令；不能用同地址的 MMIO 写入代替。不抢占已有 WMI 驱动，不暴露任意 EC 地址接口，无 modalias 自动加载。

## 接口

在 `/sys/class/hwmon/hwmonN/name` 为 `mechrevo` 的设备下，标准只读 hwmon 提供 `temp1_input`（毫摄氏度）、`fan1_input`/`fan2_input`（RPM）及标签。编号不可硬编码。

以下自定义属性只允许 root 写入，普通用户可读：

| 属性 | 写入值 | 说明 |
| --- | --- | --- |
| control | acquire / release | 显式接管、恢复接管前设置 |
| mode | office / balanced / turbo | 原生三档，保留全速位 |
| fan_control | auto / manual / curve / boost | 自动、手动、EC 自主曲线；boost 通过同一接口提供，菜单无全速选项 |
| fan_curve | 10 个百分比 | 前 5 个风扇 1，后 5 个风扇 2；对应温区上限 40/50/60/70/80°C，0..100 非递减，末档可编辑；仅 UI 限制最低 20%。写入即启用固件曲线 |
| fan1_percent / fan2_percent | 0..100 | 独立指令百分比；写入即进入手动，启动从 EC 读取当前 PWM；非精确 RPM |
| keyboard_brightness | 0..4 | 保留无关位并触发应用 |
| keyboard_rgb | R G B，各 0..50 | 三通道串行写入并校验；支持全零 |
| charging_profile | high_capacity / stationary | 固件配置，不是百分比阈值；实际充电行为仍待测 |
| last_error | 不可写 | 最近控制错误，成功重新接管后清零 |

曲线模式把两路 16 项温区表写入 0x0F00..0x0F5F，首项 duty=0 为固件启用条件，额外开启 0726.7、0727.6、07c5.7、07c6.2。温区下行阈值比上行低 3°C。不备份曲线表；写表期间不主动开启全速，成功后完全停止主机 PWM 刷新，由 EC 执行。切换性能模式（包括实体键）会先恢复曲线，再应用性能模式。用户显式恢复或切换风扇模式时退出自定义曲线；生命周期退出不写寄存器，不回写旧曲线表。表属于运行时 RAM，不是 BIOS Flash；尚未验证重启后保留，也不自动重放。

启动时读取 EC 的两路曲线表，不再用程序默认值覆盖显示。如果 EC 没有可识别的温区表，显示不可用；用户主动选择曲线模式时才初始化 30/40/60/80/100% 模板。可表示的五档曲线对应温区上限 40/50/60/70/80°C，低于首个 20°C 哨兵区可能停转；更高温区沿用各路末档值。参考项目声称的温控协议需以本机记录为准，不能据本机测试外推所有蛟龙型号。

手动调速开启 0x07c5 bit7 分离两路，使用 WMI 0x1804/0x1809 定期下发百分比×2（最大 200）。每轮结束约 800 ms 后再执行，避免固件自动回调使目标失效；不改写温度曲线表、功耗或自定义性能配置。用户显式切回自动或恢复时清除分离位，交给固件调速。驱动卸载、睡眠和重启通知只停止刷新，不写回旧 PWM；重新加载或睡眠恢复不会自动续发。不读取温度来覆盖手动目标，不再因温度异常释放或强制全速。通信失败记录错误并停止后续主动下发，等待用户显式恢复，不自动写回整套旧设置。本机 EC 自身高温兜底行为未验证，本驱动不修改固件自身保护。

加载仅监测，不改设置；读取现有风扇 PWM、曲线表和启用状态。`control=retained` 表示 EC 控制位仍开启但新驱动尚未接管，首次用户设置可重新接管。acquire 读取当时的性能、亮度、颜色和充电配置作为本次手动恢复的基线；接受此前保留的控制位和曲线。内核没有心跳、超时回收、配置文件或开机重放；用户态系统服务单独持久化 RGB、风扇和充电偏好并在启动时下发。服务停止、卸载、睡眠、正常重启/关机均不主动恢复。仅 `control=release` 执行手动恢复：恢复本次修改的标量字段，停止手动下发/自定义曲线并切回固件自动风扇；曲线下性能模式为 0 时回退办公模式。它不是 BIOS 出厂重置。恢复失败报告错误，需再次显式操作，不后台重试。

接管期间 WMI 0xB0 在驱动内处理办公→狂暴→均衡→办公，互斥锁将按键、sysfs、恢复和读写串行化。350 ms 去抖；没有实现其他 Fn 事件。睡眠恢复只读取当前 EC 状态，用户下一次设置才重新接管。Zig 服务停止不释放控制；服务重启读取驱动状态并恢复已保存的 RGB、风扇和充电偏好。硬件自身是否跨重启、断电保留尚需实测；用户态磁盘持久化与硬件保留是不同机制。

## 构建与部署

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD/driver" W=1 modules
sudo insmod driver/mechrevo_control.ko
sudo rmmod mechrevo_control
```

运行内核头文件必须匹配。正式部署用项目的 `scripts/install-system.sh`（需先安装 dkms 并构建 dist），该脚本会安装 DKMS、系统服务及开机模块加载；不会替用户在会话中安装扩展。Secure Boot 下需按本机配置对模块签名。不得同时运行参考 TUXEDO/Uniwill 驱动或本项目诊断模块。

代码参考 TUXEDO Computers GmbH 的 tuxedo-drivers v4.25.0_rc（原协议实现版权 2020–2024 TUXEDO Computers GmbH，GPL-2.0-or-later），以及 sklynic/pkgbuild-mechrevo-driver-dkms-16-pro 的 af56c84451d6e003267a886183b7deb94ec4476a。本驱动选择 GPL-2.0-only 发布，许可正文见 COPYING；本项目诊断记录提供额外本机验证依据。不是完整 TUXEDO 驱动的直接打包。

固件 RAM 曲线补充协议参考：https://github.com/nemoiwkryang/Open-Mechrevo-FanSlider ，固定提交 c6665cfc15b7ceb0d08111ea317d9ceccc2feb2e（MIT）。本机测试发现 TUXEDO 的首 duty 非零方案不会启用该 EC 的自定义温区表；补全启用条件后已验证自主维持和温度跨档。
