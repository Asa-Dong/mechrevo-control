# 百分比限充诊断

仅针对 MECHREVO / JIAOLONG Series-X6xR55xK，独立于产品驱动。

- `mechrevo_charge_probe`：固定白名单只读采样，对比 ECRR 与 WMI 邮箱。三次采样，不提供参数或写接口。
- `mechrevo_charge_limit_test`：固定 80 → 60 → 取消测试。通过 ECRW 写入实时上限和临时平台值，通过 WMI 读回存储值和门控。只接受初始平台值 7、ROM ID 首字节 `0xFF`、项目 ID `0x1A`、门控关闭、实时和存储上限为 0 的状态。没有任意地址接口，不写 ROM ID、恢复下限或存储地址。

构建：

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD/diagnostics/charge-limit" W=1 modules
```

运行前停止 `mechrevo-control-system.service` 并卸载 `mechrevo_control`，避免并发访问；完成后必须卸载诊断模块、重新加载产品模块、启动原服务。不要直接用于其他固件或已有百分比限充配置的状态。写测试会短暂将平台值设为 4，每步尝试恢复 7，并最终清零上限、触发存储及核对取消结果；恢复失败会保留 pending 标记并在卸载时重试。模块无法对硬件访问失败承诺恢复成功，必须检查日志中的恢复结果。

**加载返回 0 不等于测试通过**：模块在实验失败时也保留加载状态，便于恢复失败后的卸载重试。验收必须检查 `TEST result`、`RESTORE result`、`pending` 和 `platform_pending`。本机实际测试 `TEST result=-61`、恢复成功，说明临时门控路径没有保持激活。

早期首轮 WMI 写入实验保留在日志中；当前源码改为 ECRW 写入，与第二轮日志一致。协议来源和具体结果见 [调查记录](../../docs/validation/2026-11-10-charge-limit.md)。当前代码不用于持续限充，也不能证明电池真实停充或恢复充电。
