# 精简驱动与 Zig 系统服务集成验证

本轮完成 `driver/mechrevo_control.c`、Zig sysfs 控制后端、系统 D-Bus/Polkit 服务、GNOME 控制菜单、DKMS 配置及显式安装脚本。没有直接加载参考项目完整驱动。

## 编译与静态验证

- 当前内核 6.19.13+deb13-amd64，内核模块 W=1 编译通过，无编译警告。
- Zig 0.14.1 ReleaseSafe 构建通过；4 个测试通过：缺失接口、异常传感器/零转速、厂商状态发现、固定控制参数白名单。
- GNOME 扩展 JavaScript 语法通过；zip 内为 metadata.json、extension.js。
- shell 脚本语法及 D-Bus/Polkit XML 解析通过。
- systemd-analyze verify 通过（仅将验证副本中的 ExecStart 替换为本地构建路径，未安装服务）。
- DKMS 配置和源代码包已生成，尚未执行系统 DKMS 安装，也未测试其他内核版本。

## 本机实测

驱动加载只读，hwmon 名为 mechrevo。本轮初始固件为办公，亮度 4、RGB 0 50 50、高容量充电配置。读取实际两路转速，例如 2034/2160 RPM，温度为 CPU EC 60°C；Zig JSON 同时包含标准 hwmon 列表及 vendor 控制状态。

会话 D-Bus 测试通过：连续 10 次 GetStatus、非法签名拒绝、只读会话服务拒绝 Set。采样 RSS 约 3.4 MiB。

系统 D-Bus 实机脚本通过：

1. 普通账户可以读状态，nobody 写入被拒绝且不接管控制。
2. 非法亮度值被拒绝且不接管。
3. 亮度、RGB、充电配置、全速位和三档模式均经过 Set→sysfs→EC；灯光和充电写回原值、全速位设为 0，模式短暂设为 balanced。
4. control release 后，模式、全速、亮度、RGB、充电配置均与接管前快照一致。
5. SIGTERM 结束服务后正常恢复，control=firmware。
6. SIGKILL 强制结束服务时 control 仍为 active；没有用户态清理，约 120 秒内核租约到期后恢复原设置，control=firmware。

直接绕过 Zig 的内核接口测试通过：未接管时写入拒绝、keepalive 不会重新接管、非法亮度/RGB/充电枚举/模式拒绝、普通账户无 sysfs 写权限。接管后切到 balanced，再卸载模块；独立只读探针确认办公 0xA0、控制位 0x80，原设置恢复。见 `kernel-unload-2026-11-09.log`。

## 授权测试中发现的问题

安装 Polkit action 后，第一版测试中从管理员测试进程 runuser 到 nobody 的请求获得了授权。尚未进一步确定本机 Polkit 对这种继承会话进程的判定原因，不把它归因于某条已发现的全局规则。

服务增加明确的前置约束：非 root 调用者必须拥有 logind 活跃、本地的桌面会话，再依据调用消息的唯一 D-Bus sender 向 Polkit 请求授权；不能依赖传入参数声明身份，也不能仅凭继承的会话认定权限。更新后带真实 action 配置的测试通过，nobody 无法写入。当前用户 asa 的 Display=2、State=active，符合桌面会话前置条件。桌面认证弹窗的允许/取消流程仍需真实 GUI 测试。

## 结束状态与边界

- 系统测试服务已结束；临时系统 D-Bus/Polkit 配置已删除。
- 精简驱动和独立探针均已卸载。未启用开机服务，未在 GNOME 会话安装扩展。
- 当前固件办公模式；没有残留硬件接管。
- 模式键具体协议、灯色、RGB、亮度、全速等更早的视觉验证见 `2026-11-09-rgb-fan-key.md`。
- 本轮精简驱动中的 WMI 处理已实现，但未再次请求用户进行实体按键循环；不能把先前诊断模块的验证等同于正式驱动全部路径实测。
- 休眠/恢复、重启通知、失败重试和长期运行尚未实机验证。其他 Fn 事件未实现。
- GNOME 控制菜单已实现，真实布局、交互、Polkit 弹窗及扩展启停尚未测试。
- 充电真实效果、三档负载表现、自定义风扇曲线等仍保持先前验证边界。

## 用户授权后的正式安装

用户明确要求“安装吧”后完成：

- DKMS 0.1.0 已构建、签名并安装到 6.19.13+deb13-amd64、7.1.0-rc3-manual、7.1.3+deb13-amd64、7.1.8+deb13-amd64。
- 当前内核加载 mechrevo_control；模块开机加载配置写入 /etc/modules-load.d/mechrevo-control.conf。
- mechrevo-control-system.service 已启用并运行，系统 D-Bus 只读查询成功。经实际 systemd 沙箱执行一次原值 mode 写入及 release 成功，最终 control=firmware、last_error=0。
- D-Bus/Polkit 正式配置及 /usr/local/libexec/mechrevo-control 已安装。
- GNOME 扩展 v2 安装在用户的 .local/share/gnome-shell/extensions/mechrevo-control@local，已加入 enabled-extensions，保留其他扩展设置。
- 当前 GNOME 48.7 会话尚未发现新扩展；ReloadExtension 返回未实现。需要用户注销并重新登录后加载，未强制结束桌面会话。实际菜单、认证弹窗和正式驱动按键联动仍待登录后确认。

DKMS 只报告 CLEAN 字段废弃提示，四个内核均构建安装成功；其他内核的运行时验证尚未进行。
