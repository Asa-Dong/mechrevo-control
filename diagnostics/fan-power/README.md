# 拔电后风扇状态只读快照

`mechrevo_fan_power_probe` 仅匹配 MECHREVO / JIAOLONG Series-X6xR55xK，通过 ECRR 读取固定控制位、温度、PWM 镜像和 `0x0F00–0x0F5F` 的 96 字节曲线表。没有写入命令、参数或用户态接口。

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD/diagnostics/fan-power" W=1 modules
```

采样时先停止系统服务、卸载产品驱动，以避免并发访问。卸载本探针后先加载产品驱动并记录保留状态，再启动原系统服务；否则启动时重放偏好可能掩盖原现场。不要使用 `fan-table-backup` 代替本探针，该历史模块卸载时会写回表内容。

实测记录见 [电池供电风扇调查](../../docs/validation/2026-11-10-fan-power.md)。
