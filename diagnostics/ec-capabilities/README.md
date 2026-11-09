# 一次性只读能力探测

这是内核诊断工具，不是产品驱动。仅匹配 MECHREVO / JIAOLONG Series-X6xR55xK，读取固定白名单寄存器，没有参数、写入方法或用户态接口。通过内核 ACPI API 执行本机 DSDT 的 ECRR 方法；读取能力标志不等于验证写入有效。

构建：

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD/diagnostics/ec-capabilities" modules
```

需要实际探测时手动加载；执行完立即卸载，不设置开机加载：

```sh
sudo insmod diagnostics/ec-capabilities/mechrevo_probe.ko
sudo rmmod mechrevo_probe
sudo journalctl -k -b --no-pager | rg mechrevo_probe
```

每次探测仅记录当时读数。当前结果见 `docs/validation/ec-capabilities-2026-11-09.log`。编译与加载操作需要匹配当前内核的 headers 和相应系统权限。Zig 用户态服务不会自动加载这个模块。
