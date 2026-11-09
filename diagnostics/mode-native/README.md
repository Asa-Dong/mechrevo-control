# 原生模式键只读采样

精确匹配 MECHREVO / JIAOLONG Series-X6xR55xK，只通过 ECRR 每 500 ms 读取 0751（模式）、0741（手动控制）、0727（自定义配置）、07c6（风扇曲线）。仅在值变化时记录内核日志。不注册 WMI 驱动、不写 EC、不改变按键行为。

五分钟后自动停止采样，模块仍需卸载。采样是顺序读取，不能视为原子快照，也可能遗漏短于采样间隔的状态。

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD/diagnostics/mode-native" modules
sudo insmod diagnostics/mode-native/mechrevo_native_watch.ko
sudo dmesg | rg mechrevo_native_watch
sudo rmmod mechrevo_native_watch
```

将每次按键与用户确认的灯色、模式名称关联；名称确认不等于功耗、性能或风扇策略已验证。
