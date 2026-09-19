# lab1 阶段一: 汇编引导入口。
# QEMU virt 复位后 PC=0x1000(Boot ROM), 随后把内核加载到 0x80000000 并跳转至此。
# 此刻还没有 C 运行时环境, 必须先建好栈、关中断, 才能 call start()。
#
# qemu -kernel 把内核加载到 0x80000000, 并让每个 hart(CPU)都从 _entry 开始执行。
# kernel.ld 保证 _entry 恰好落在 0x80000000。

#include "param.h"       // NCPU / BOOT_HART
#include "course_sid.h"  // LAB1_STACK_KB(个性化栈大小)

.section .text
.global _entry
_entry:
        # 关闭 M 态所有中断: 未建立陷阱处理环境前, 任何中断都会让系统跑飞。
        csrw mie, zero

        # 读取 hartid, 只有主核(BOOT_HART)继续引导, 其余从核进入 wfi 自旋等待。
        csrr t0, mhartid
        li t1, BOOT_HART
        bne t0, t1, park

        # 为 C 语言配置初始栈:
        #   stack0 在 start.c 中定义, 每核 LAB1_STACK_KB KB, 16 字节对齐。
        #   sp = stack0 + (hartid + 1) * LAB1_STACK_KB * 1024
        # 每个 hart 都算一遍(即使从核不执行 C 代码, 也保证约定一致)。
        la sp, stack0
        li a0, LAB1_STACK_KB * 1024
        addi a1, t0, 1
        mul a0, a0, a1
        add sp, sp, a0

        # 进入机器模式初始化 (kernel/start.c)。
        call start

        # start() 通过 mret 切换到 S 态, 正常情况下永远不会返回这里。
spin:
        j spin

# 从核: 不触碰任何共享状态, 等待主核完成引导(后续实验再由主核唤醒)。
park:
        wfi
        j park
