# 临时模式键联动验证

可用 `timeout_seconds=60` 缩短测试（允许 30–180 秒，默认 180）。`timeout_only=1` 仅处理第一个按键事件，然后等待超时恢复，用于隔离超时路径；此模式下后续事件被忽略。

精确匹配本机 DMI，绑定已验证的 WMI GUID，临时启用手动控制位。每个 0xB0 事件按办公 0xA0→狂暴 0x30→均衡 0x20→办公循环，使用 0xB0 掩码写 0x0751 并读回，保留其他位。350 ms 内重复事件忽略，测试时每次按键间隔至少 2 秒。

完成三次切换、180 秒超时、处理错误、设备解绑或卸载时恢复原模式和原控制位。互斥锁串行化事件、超时和恢复。恢复每项重试三次，失败保留标志供后续清理重试；崩溃、休眠等情形尚无生产级恢复保障。测试期间保持唤醒。

只接受已知原生模式、无自定义曲线、未被手动接管、初始温度不高于 75°C；每次按键检查温度低于 85°C。不会抢占其他驱动已绑定的设备。没有用户态写入接口，不是常驻产品驱动。

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD/diagnostics/mode-key-cycle" modules
sudo insmod diagnostics/mode-key-cycle/mechrevo_key_cycle.ko
# 三次按键后检查日志，并卸载；超时只恢复控制权，不自动卸载。
sudo dmesg | rg mechrevo_key_cycle
sudo rmmod mechrevo_key_cycle
```
