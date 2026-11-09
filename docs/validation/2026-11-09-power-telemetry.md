# CPU / 独显温度与功耗

GNOME v11 主菜单显示 CPU 温度及封装平均功耗、NVIDIA 独显温度及驱动报告的功耗，风扇转速单独一行。顶栏保持原有紧凑显示。

CPU：本机 AMD 9955HX 使用 /sys/class/powercap/intel-rapl:0（name=package-0）的 energy_uj，以单调时钟采样间隔计算 W。仅包含封装域，未把 core 子域重复相加。处理计数器回绕；首次采样、不可读、间隔大于 30 秒或异常值返回 null。小于 200ms 的重复请求复用上次读数。采样随 GetStatus 请求发生，不增加心跳或控制循环。

GPU：通过 PCI vendor/class 找到 NVIDIA 显示设备，仅在 runtime_status=active 时使用 NVML；按 PCI 地址获取设备，读取 nvmlDeviceGetTemperature 和 nvmlDeviceGetPowerUsage（mW 换算 W）。每次读取后关闭 NVML；休眠、驱动缺失或单项不支持时对应值为 null。功耗是驱动报告的 GPU 及相关电路功耗，不是整机墙上功耗；本代 NVML 接口通常报告约一秒平均值。没有温控或功率限制写入。

系统服务为 NVML 开放 char-nvidia 设备组的 rw 访问（ioctl 所需），DevicePolicy=closed；PrivateDevices=no，原有只读系统与空 capability 集合保留。

实测系统 D-Bus：CPU 47.125°C / 14.32 W，GPU 41°C / 5.982 W（瞬时记录，随负载变化）。独立 GNOME 会话截图亦读到 CPU 48°C / 15.6 W、GPU 41°C / 17.9 W。Zig 单元测试包括单位换算、回绕、无效采样间隔，均通过；菜单布局、按钮/滑条、写入反馈高度检查通过。

来源：
- https://cdn.kernel.org/doc/html/latest/power/powercap/powercap.html
- https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html

## 重启后独显读数缺失修复

本次开机服务在约 6.01 秒启动，nvidia-persistenced 在约 6.55 秒完成。服务内 strace 显示 /dev/nvidia0 返回 EPERM，宿主相同采集代码可以读取。原 DeviceAllow 按路径解析早于设备节点创建，造成规则遗漏。改为依赖并排序在 modprobe@nvidia.service 之后，且在已启动的 nvidia-persistenced.service 之后，使用 char-nvidia 匹配已注册设备组及后续创建的节点。无需开放所有设备，也无需 UVM 权限。重启服务后 D-Bus 读到 43°C / 5.399 W，后续查询仍成功；尚未为了验证而重启整机。

前端缺失值统一为半角 -，功耗不可用时不追加 W。
