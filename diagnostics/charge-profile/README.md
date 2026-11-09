# 充电配置受控切换诊断

仅用于 MECHREVO / JIAOLONG Series-X6xR55xK 的验证，不是产品驱动。该工具会短暂写入硬件，不能作为只读探测运行。

流程：检查 DMI 和能力位；保存 0x07A6 的 bits5:4 及 0x0741 的 bit0；短暂启用手动控制；将充电配置切为 stationary；等待一秒读回；先恢复充电配置再恢复控制位。写入采用读改写，保存其他位。原本已为 stationary 时跳过测试。

若还原失败，模块保留原值并留驻，卸载时重试。日志中 RESTORE FAILED / GATE_RESTORE_FAILED 必须人工处理，不得将“模块已卸载”当作还原成功。该机制不能保证断电/内核崩溃期间的恢复。

构建：

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD/diagnostics/charge-profile" modules
```

诊断执行（不由服务自动调用）：

```sh
sudo insmod diagnostics/charge-profile/mechrevo_charge_test.ko
sudo journalctl -k -b --no-pager | rg mechrevo_charge_test
# 确认 RESTORED 与 GATE_RESTORED 后卸载
sudo rmmod mechrevo_charge_test
```

依据：TUXEDO v4.25.0_rc 的 uw_set_charging_profile；Linux 上游 uniwill-acpi.c 的 uniwill_ec_init、uniwill_set_charge_type；本机 DSDT ECRR/ECRW。

成功读回不证明实际充电阈值。只测试了 high_capacity → stationary → high_capacity，没有验证 balanced、长期保持、重启保持或电池实际停止充电行为。
