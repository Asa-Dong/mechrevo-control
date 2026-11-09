# 风扇全速增强测试

只测试参考驱动 set_full_fan_mode 使用的 0x0751 bit6，不写自定义风扇表或 0x180x 转速控制值。精确匹配本机 DMI；初始 EC 温度高于 75°C、已有自定义风扇表或全速位时拒绝测试。

保存控制位，短暂启用手动位和全速位；每两秒读取双路 RPM 和温度，共 5 次。读错或温度达到 85°C 时提前恢复；恢复原全速位与手动位后，再观察 5 次 RPM 回落。温度检查仅用于诊断，不能代替固件保护或生产级控制守护。

实际转速通过高/低/高字节读取，检测高字节跨界后重试。该策略降低组合读数撕裂风险，不宣称硬件原子快照。

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD/diagnostics/fan-boost" modules
sudo insmod diagnostics/fan-boost/mechrevo_fan_test.ko
sudo journalctl -k -b --no-pager | rg mechrevo_fan_test
# 确认 BOOST_RESTORED 和 GATE_RESTORED 后
sudo rmmod mechrevo_fan_test
```

尚未验证任意 PWM、目标 RPM、自定义曲线或风扇停止。若恢复失败，必须处理日志中的错误，不应直接用于产品控制。
