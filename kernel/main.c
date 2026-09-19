//
// lab1 阶段三/四: S 态内核主入口。
// 由 start() 通过 mret 跳转到这里执行, 是内核核心的第一段 C 代码。
// 目标: 在控制台上按学号个性化协议输出启动 Banner。
//
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "course_sid.h"
#include "defs.h"

//
// 输出一行文本, 并返回该行所有字节的 ASCII 码累加和(32 位回绕)。
// 输出与统计在同一遍循环里完成, 因此校验和必然与实跑输出逐字节一致。
//
static uint32
emitline(char *s)
{
  uint32 chk = 0;

  for (; *s != '\0'; s++) {
    consputc(*s & 0xff);
    chk = (chk + (uint32)(*s & 0xff)) & 0xffffffffu;
  }

  return chk;
}

//
// 生成协议 0 的正文到 buf(含行尾换行), 返回字节数。
// 数值部分调用 fmtuint, 与 printf 的 %d / %x 使用同一份转换实现。
//
static int
buildbanner(char *buf, int cap)
{
  int n = 0;
  char *p;
  char num[24];
  int k, i;

  // "OSLAB1 sid="
  for (p = "OSLAB1 sid="; *p && n < cap - 1; p++)
    buf[n++] = *p;

  // 学号十进制(%d 的等价转换)
  k = fmtuint((uint64)COURSE_SID, 10, num);
  for (i = 0; i < k && n < cap - 1; i++)
    buf[n++] = num[i];

  // " mod97=0x"
  for (p = " mod97=0x"; *p && n < cap - 1; p++)
    buf[n++] = *p;

  // 学号 % 97 的小写十六进制, 无前导零
  k = fmtuint((uint64)(COURSE_SID % 97), 16, num);
  for (i = 0; i < k && n < cap - 1; i++)
    buf[n++] = num[i];

  // 行尾换行
  if (n < cap - 1)
    buf[n++] = '\n';

  buf[n] = '\0';
  return n;
}

//
// 学号 Banner 协议(见 kernel/course_sid.h 的 LAB1_BANNER_PROTOCOL):
//   协议 0: 输出一行, 行尾换行;
//   协议 1: 在协议 0 基础上, 每个字节(含行尾换行)后紧跟一个 '.';
//   协议 2: 在协议 0 基础上, 换行后追加一行 [chk=校验和](校验和为最后输出)。
//
static void
banner(void)
{
  char line[64];
  uint32 chk;
  int n, i;

  n = buildbanner(line, sizeof(line));

  switch (LAB1_BANNER_PROTOCOL) {
  case 1:
    // 逐字节输出, 每个字节(含行尾 '\n')后紧跟一个 '.'。
    for (i = 0; i < n; i++) {
      printf("%c", line[i]);
      printf(".");
    }
    break;

  case 2:
    // 输出正文(ASCII 累加和/32 位回绕), 换行后回显校验和。
    chk = emitline(line);
    printf("[chk=%d]\n", chk);
    break;

  default:
    // 协议 0: 直接输出正文本行。
    printf("%s", line);
    break;
  }
}

// start() 通过 mret 降级到 S 态后跳转到这里。
void
main()
{
  consoleinit();

#ifdef LAB1_SELFTEST
  // lab1 自测(默认关闭): make LAB1_SELFTEST=1 时打开, 见 kernel/selftest.c。
  // 打开后只跑用例、不再输出 Banner, 以免干扰逐字节比对。
  if (selftest())
    return;
#endif

  banner();

  // 本阶段还没有进程与调度器, 也不开启中断: 输出完成后停在这里等待。
  for (;;)
    asm volatile("wfi");
}
