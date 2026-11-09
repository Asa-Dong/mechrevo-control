# 三档软件写入验证

精确匹配本机 DMI，仅重放已通过原生按键及用户灯色确认的 0x0751 状态。保存初始模式，临时启用 0x0741 bit0，以 0xB0 掩码依次写入 0x20、0x30、0xA0，每档保持约 8 秒，每秒读回并检查温度，随后恢复初始模式和控制位。保留掩码外其他位，不写风扇曲线或功耗参数。

仅允许已知原生模式、未接管控制、无自定义配置/曲线、CPU 不高于 75°C 时开始；达到 85°C 或读写失败则进入恢复。恢复失败会保留模块供卸载重试，不保证崩溃或断电时恢复。

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD/diagnostics/mode-switch" modules
sudo insmod diagnostics/mode-switch/mechrevo_mode_test.ko
sudo rmmod mechrevo_mode_test
```

测试期间不按模式键。本模块不处理 WMI 按键事件，也不是生产驱动。读回及灯色切换不能证明实际性能/功耗策略变化。
