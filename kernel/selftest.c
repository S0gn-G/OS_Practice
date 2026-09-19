//
// lab1 自测用例(验收要求 §4: printf 边界与协议格式边界)。
// 默认不编译; 用 `make LAB1_SELFTEST=1` 打开, 此时内核只跑用例、不输出 Banner,
// 便于把 QEMU 串口输出与 tests/expect_selftest.txt 直接比对。
//
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "course_sid.h"
#include "defs.h"

static int fails;

static void
chk(char *name, int got, int want)
{
  if (got != want) {
    printf("FAIL: %s: got %d, want %d\n", name, got, want);
    fails++;
  } else {
    printf("ok: %s\n", name);
  }
}

// 返回 1 表示本轮已经跑过用例(调用方应直接返回, 不再打印 Banner)。
int
selftest(void)
{
  char buf[64];
  int i, n;

  printf("== lab1 selftest ==\n");

  //
  // printf 边界用例: 每个用例同时核对"输出文本"和"返回的字符数"。
  // 串口上打印的文本由 tests/expect_selftest.txt 逐字节核对。
  //

  // %d: 0 / 负数 / 最大最小整数
  printf("T1  [%d]\n", 0);
  // "T2  [-1]\n" = 9 字节, printf 的返回值应等于实际写出的字符数
  n = printf("T2  [%d]\n", -1);
  chk("ret(%d -1)", n, 9);
  printf("T3  [%d]\n", 12345);
  printf("T4  [%d]\n", -12345);
  printf("T5  [%d]\n", 2147483647);  // INT_MAX
  printf("T6  [%d]\n", -2147483648); // INT_MIN(取负不能溢出)

  // %x: 小写、带 0x 前缀、无前导零
  printf("T7  [%x]\n", 0);
  printf("T8  [%x]\n", 0x48);
  printf("T9  [%x]\n", 0xdeadbeef);
  printf("T10 [%x]\n", 4294967295u); // UINT_MAX

  // %s: 空串 / 普通串 / NULL
  printf("T11 [%s]\n", "");
  printf("T12 [%s]\n", "hello");
  printf("T13 [%s]\n", (char *)0);

  // %c 与 %%
  printf("T14 [%c%c]\n", 'A', 'z');
  printf("T15 [100%%]\n");

  // 返回值 = 实际写出的字符数(n=6: 'T','1','6',' ','\n' 之外还有结尾)
  n = printf("T16 [%s]", "1234567890");
  printf("\n");
  chk("ret(T16)", n, 16);

  // 未知占位符原样输出, 便于发现格式错误
  printf("T17 [%q]\n");

  //
  // 协议格式边界用例: 对"将要输出的那一行"按协议 1/2 展开。
  // 与 main.c 的 buildbanner() 同源(fmtuint), 逐字节核对展开结果。
  //
  n = 0;
  {
    char *p;
    char num[24];
    int k;

    for (p = "OSLAB1 sid="; *p && n < (int)sizeof(buf) - 1; p++)
      buf[n++] = *p;
    k = fmtuint((uint64)COURSE_SID, 10, num);
    for (i = 0; i < k && n < (int)sizeof(buf) - 1; i++)
      buf[n++] = num[i];
    for (p = " mod97=0x"; *p && n < (int)sizeof(buf) - 1; p++)
      buf[n++] = *p;
    k = fmtuint((uint64)(COURSE_SID % 97), 16, num);
    for (i = 0; i < k && n < (int)sizeof(buf) - 1; i++)
      buf[n++] = num[i];
    buf[n++] = '\n';
    buf[n] = '\0';
  }

  // 协议 1: 每个字节(含行尾 '\n')后跟 '.'
  printf("P1  ");
  for (i = 0; i < n; i++)
    printf("%c.", buf[i]);
  printf("\n");

  // 协议 2: 校验和 = 行内所有字节 ASCII 码之和(32 位回绕)。
  // 本用例只核对"与预期文件的校验和一致"(2290), 任何偏差都会在此报错。
  {
    uint32 sum = 0;
    for (i = 0; i < n; i++)
      sum = (sum + (uint32)(buf[i] & 0xff)) & 0xffffffffu;
    printf("P2  [chk=%d]\n", sum);
    chk("chk(banner line)", (int)sum, 2290);
  }

  printf("== selftest %s (fails=%d) ==\n", fails ? "FAILED" : "PASSED", fails);

  return 1;
}
