# 模式键观察器

精确匹配机型后临时注册 WMI 事件 GUID ABBC0F72-8EA1-11D1-00A0-C90629100000 的观察器。当前版本会保存并临时启用 0x0741 bit0，以验证驱动接管后的事件；卸载时恢复此位。只记录事件，不注入键盘输入，也不直接设置性能档位。

早期完全不写 EC 的版本保存在 `native-observer.c.txt`，其测试未捕获到 WMI 事件，但物理按键改变了固件模式状态。

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD/diagnostics/mode-key" modules
sudo insmod diagnostics/mode-key/mechrevo_key_probe.ko
# 按键后查看，测试结束务必卸载
sudo journalctl -k -b --no-pager | rg mechrevo_key_probe
sudo rmmod mechrevo_key_probe
```

如果对应 WMI 设备已被其他驱动绑定，不应解绑或抢占。确认 observer attached 后再请求用户按键；临时手动位可能改变其他 Fn 键处理方式，不作为常驻服务。卸载后必须确认 gate_restore=0，并独立读取原始位。恢复失败或系统崩溃不能假定原值已恢复。
