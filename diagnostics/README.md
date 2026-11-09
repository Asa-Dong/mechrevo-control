# 协议诊断资料

这里保存硬件协议调查期间的独立内核模块与参考实验。它们不由产品构建、安装或 CI 自动运行，也不属于运行依赖。

诊断模块可能直接修改 EC，并且部分记录描述旧版本行为。使用前阅读各目录 README 和源码；不要与产品驱动或其他控制驱动同时绑定/访问硬件。

当前产品接口见 [driver/README.md](../driver/README.md)，测试入口见 [tests/README.md](../tests/README.md)，历史证据见 [docs/validation](../docs/validation)。
