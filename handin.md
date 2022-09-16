# OS Lab 1

## 实现思路

1. > 仅CPU0进入初始化流程，其余CPU直接退出。

   在启动后调用 `cpuid()`, 除了 cpuid 为 0 的 cpu 全部退出.

2. > 初始化`.bss`段，将其填充为`0`。

   因为 linker.ld 脚本中提供了 `.bss` 段的起始地址, 链接名分别为 `edata` 和 `end`. 在 `main.c` 中声明 `extern` 变量, 链接器就会把段起始地址放入变量, 程序中使用 `memset` 把 `edata` 到 `end` 的地址初始化为 `0` 即可.

3. > 在`main.c`中使用`define_early_init`定义函数1，该函数将`hello[]`填充为`Hello world!`。

   > 在`main.c`中使用`define_init`定义函数2，该函数通过uart输出`hello[]`的内容。

   使用 `define_early_init` 和 `define_init` 宏在两个启动阶段注册对应函数即可.
