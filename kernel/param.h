// lab1 内核参数。
// 本实验只保留当前阶段真正会用到的常量; 进程/文件系统等参数
// (NPROC、NOFILE、FSSIZE 等)属于后续实验内容, 届时随增量包引入。

#define NCPU      8 // 最大 CPU 核数, 每核一个启动栈
#define BOOT_HART 0 // 引导核编号, 其余从核在 entry.S 中挂起
