# 固定 RGB 颜色测试

仅限精确 DMI 匹配机型。保存 0x0769/6A/6B 三个 EC 颜色通道（0–50），短暂启用 0x0741 bit0，将颜色设为红色（原本为纯红则用蓝色），保持 8 秒，读回，再恢复原色及控制位。拒绝不明通道范围、全零原色和已开启彩虹模式的情况。0x0767 bit5 是应用命令位。

不修改亮度、充电、风扇或功耗参数；不是长期运行驱动。若 restore 返回失败须检查恢复状态，不能仅凭卸载判断成功。

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD/diagnostics/rgb-color" modules
sudo insmod diagnostics/rgb-color/mechrevo_rgb_test.ko
sudo journalctl -k -b --no-pager | rg mechrevo_rgb_test
# 确认恢复成功后
sudo rmmod mechrevo_rgb_test
```
