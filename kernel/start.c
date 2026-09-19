//
// lab1 阶段二: 机器模式(M 态)初始化, 然后把 CPU 降到监管者模式(S 态)。
// 复位后 CPU 处于 M 态; 内核主体(S 态)在取指前必须已经配好 PMP,
// 否则 mret 之后的第一条取指就会触发 Instruction Access Fault。
//
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "course_sid.h"
#include "defs.h"

void main();

// entry.S 为每个 CPU 准备一个栈, 大小由个性化参数 LAB1_STACK_KB 决定
// (严禁硬编码 4096), 并按 16 字节对齐(RISC-V ABI 要求 sp 16 字节对齐)。
__attribute__((aligned(16))) char stack0[LAB1_STACK_KB * 1024 * NCPU];

// entry.S 以 M 态、在 stack0 上跳转到这里。
void
start()
{
  // 1. 设置 mstatus.MPP = S, 使 mret 之后运行在 S 态。
  uint64 x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // 2. 把 S 态入口地址写入 mepc(mret 的返回地址)。
  //    需要 -mcmodel=medany, 内核才会用 PC 相对寻址取得 main 的绝对地址。
  w_mepc((uint64)main);

  // 3. 本阶段尚未启用分页: satp = 0, S 态直接使用物理地址。
  w_satp(0);

  // 4. 把异常与中断委托给 S 态处理, 为后续实验的时钟/外设中断做准备。
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_mie(r_mie() | MIE_STIE); // M 态放行 S 态定时器中断
  w_sie(r_sie() | SIE_SEIE | SIE_STIE);

  // 5. 物理内存保护: 授予 S/U 态全部物理地址空间的读/写/执行权限。
  //    NAPOT 编码 (0x3fffffffffffff) 覆盖整个地址空间, 权限位 0xf = RWX。
  //    漏掉这两行, 新版 QEMU 上 mret 之后的第一次取指即 fault(全程无输出)。
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // 6. 允许硬件自动更新页表项的 A/D 位(后续实验需要用到的环境配置)。
  w_menvcfg(r_menvcfg() | MENVCFG_ADUE);

  // 7. 在 tp 中保存本核 hartid, 供 cpuid() 使用(每个 hart 都要设置)。
  w_tp(r_mhartid());

  // 8. 降级到 S 态并跳转到 main()。
  asm volatile("mret");
}
