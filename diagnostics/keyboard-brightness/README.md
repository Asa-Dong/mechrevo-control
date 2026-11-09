# 键盘亮度受控测试

仅用于 MECHREVO / JIAOLONG Series-X6xR55xK。保存当前亮度和 0x0741 bit0，临时启用手动控制，亮度切至 1 档（原本为 1 则切至 4），等待 10 秒读回，然后恢复亮度和控制位。

写 0x078C 时保留低四位，亮度位使用 bits7:5，并设置 bit4 作为 apply 命令；与 TUXEDO v4.25.0_rc uniwill_write_kbd_bl_brightness 一致。apply 位是命令位，不把其自动清除视作失败。不写 RGB 颜色、风扇或功耗寄存器。

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD/diagnostics/keyboard-brightness" modules
sudo insmod diagnostics/keyboard-brightness/mechrevo_keyboard_test.ko
sudo journalctl -k -b --no-pager | rg mechrevo_keyboard_test
# 确认 RESTORED 与 GATE_RESTORED 后卸载
sudo rmmod mechrevo_keyboard_test
```

模块使用固定地址和精确 DMI 限定；不提供通用 EC 写入接口。如果恢复失败，模块保留原值，卸载重试并记录日志；断电/崩溃不保证恢复。诊断工具不随 Zig 服务自动运行。
