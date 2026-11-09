# D-Bus API

系统总线名/接口：`io.github.mechrevo.Control1`；对象：`/io/github/mechrevo/Control1`。

## GetStatus() → s

返回 `schema_version=1` 的 JSON，包含 CPU 温度/功耗、GPU 状态/温度/功耗、风扇、温度传感器、电池和可空的 `vendor` 控制状态。不可用项为 `null`，列表缺失设备时为空。GPU 的 `state` 为 `active`、`suspended`、`transitioning` 或 `unavailable`。

GPU 活动时复用 NVML 会话读取温度/功耗；休眠或切换状态时不调用 NVML，两项为 `null`，不显示旧读数或假定 0 W。`active` 表示 PCI 运行状态，传感器不可用时仍可为 `active`。驱动丢失/会话失效后至少等待 30 秒再尝试建立会话。

`vendor.control`：`firmware`（未接管）、`active`（已接管）、`retained`（EC 留有控制状态但当前驱动未接管）、`restore_failed`（控制错误，需用户手动恢复）。后者为既有协议名称，也用于写入错误后的状态。

## Set(s setting, s value)

仅系统服务接受写入；固定键值白名单、活动本地桌面会话检查与 Polkit 授权，root 可管理调用。属性列表见 [驱动接口](../driver/README.md)。`control` 对外只接受 `release`；内部按需 acquire。

错误包括 `InvalidArgs`、`AccessDenied`、`io.github.mechrevo.Error.WriteFailed` 和 `io.github.mechrevo.Error.ReadFailed`。`io.github.mechrevo.Error.SaveFailed` 表示硬件已应用但磁盘保存失败；不得把它显示为已保存。错误不会触发全量自动还原。成功设置 RGB/充电/风扇后保存相应偏好，`control=release` 在用户态应用固定默认值（办公、自动风扇、亮度 1、RGB 50/50/50、高容量）并保存 RGB/风扇/充电预设。底层 sysfs release 仍为驱动释放操作，不等同于 D-Bus 的固定默认值。

```sh
busctl --system call io.github.mechrevo.Control1 /io/github/mechrevo/Control1 io.github.mechrevo.Control1 GetStatus
busctl --system call io.github.mechrevo.Control1 /io/github/mechrevo/Control1 io.github.mechrevo.Control1 Set ss charging_profile stationary
busctl --system call io.github.mechrevo.Control1 /io/github/mechrevo/Control1 io.github.mechrevo.Control1 Set ss fan_curve '30 40 60 80 100 30 40 60 80 100'
```

1.0.0 不再提供预发布的 `fan_boost` 字段/写入键，使用 `fan_control=auto/boost`。服务无参数运行在用户会话总线上，仅支持只读；`--system` 为系统服务，`--once` 打印一次状态，`--version` 打印版本。

充电配置 `charging_profile` 支持 `high_capacity`、`balanced`、`stationary`。键盘自动关灯秒数是 GNOME 扩展的用户偏好 `keyboard-idle-seconds`（0–300），不新增 sysfs 或 D-Bus 属性；扩展通过现有亮度接口关灯、恢复。
