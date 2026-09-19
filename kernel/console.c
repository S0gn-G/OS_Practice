//
// lab1 阶段三: 16550a UART 轮询驱动与最小控制台。
// 层次: printf.c(格式化) -> consputc()(控制台上层接口) -> uartputc_sync()(轮询硬件)。
//
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

// UART 寄存器在物理内存中的偏移(memlayout.h 给出基址 UART0 = 0x10000000)。
#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

#define ReadReg(reg)     (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

#define RHR 0 // receive holding register (读)
#define THR 0 // transmit holding register (写一个字节即发送)
#define IER 1 // interrupt enable register
#define FCR 2 // FIFO control register
#define LCR 3 // line control register
#define LSR 5 // line status register

#define FCR_FIFO_ENABLE (1 << 0)
#define FCR_FIFO_CLEAR  (3 << 1)
#define LCR_EIGHT_BITS  (3 << 0)
#define LCR_BAUD_LATCH  (1 << 7) // 置位后进入波特率设置模式
#define LSR_TX_IDLE     (1 << 5) // THR 已空, 可以写入下一个字符

//
// 同步(轮询)输出一个字符。不使用中断, 因此可以在任意上下文调用。
// 写数据寄存器前必须等待 LSR.TX_IDLE(第 5 位), 否则字符会被硬件丢弃。
//
void
uartputc_sync(int c)
{
  // 等待发送保持寄存器空闲。
  while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);
}

//
// 控制台字符输出上层接口, 实际字符交给底层 UART 驱动。
//
void
consputc(int c)
{
  uartputc_sync(c);
}

//
// 控制台初始化: 本阶段只完成 UART 的基本配置(波特率/字长/FIFO),
// 中断相关配置留到后续实验。
//
void
consoleinit(void)
{
  // 关闭中断, 本阶段使用轮询而不是中断。
  WriteReg(IER, 0x00);

  // 进入波特率设置模式, 写入 38.4K 的分频值。
  WriteReg(LCR, LCR_BAUD_LATCH);
  WriteReg(0, 0x03); // 分频值低字节
  WriteReg(1, 0x00); // 分频值高字节

  // 退出波特率设置模式, 8 位字长、无校验。
  WriteReg(LCR, LCR_EIGHT_BITS);

  // 复位并启用 FIFO。
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);
}
