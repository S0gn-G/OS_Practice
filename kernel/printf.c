//
// lab1 阶段四: 最小 printf 格式化输出。
// 本层只负责"解析格式串 -> 字符流", 真正的字符发送交给下层 consputc(),
// 保持 printf -> consputc -> uartputc_sync 的分层结构。
//
// 支持: %d(有符号十进制) %s(字符串) %x(小写十六进制, 带小写 0x 前缀、无前导零)
//       %%(字面百分号), 以及 %c / %u / %p / %ld / %lu / %lx。
//
#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

static char digits[] = "0123456789abcdef";

//
// 除基取余法把无符号数转成文本: 先得到低位到高位, 再逆序写入 buf,
// 返回写出的字节数(不含结尾 '\0')。fmtuint 是格式化层与上层
// (main.c 的 Banner)共用的唯一一份数值转换实现。
//
int
fmtuint(uint64 x, int base, char *buf)
{
  char tmp[24];
  int k = 0;
  int n = 0;

  do {
    tmp[k++] = digits[x % base];
  } while ((x /= base) != 0);

  // tmp 中是逆序的低位->高位, 逆序搬回 buf。
  while (k > 0)
    buf[n++] = tmp[--k];

  buf[n] = '\0';
  return n;
}

//
// 打印一个整数并返回写出的字符数。sign 为真时按有符号数处理。
//
static int
printint(uint64 x, int base, int sign)
{
  char buf[24];
  int n, k, i;

  if (sign && (int64)x < 0) {
    consputc('-');
    k = 1;
    // 取绝对值用无符号运算完成, 避免 -INT64_MIN 的有符号溢出(UB)。
    x = (uint64)0 - x;
  } else {
    k = 0;
  }

  n = fmtuint(x, base, buf);
  for (i = 0; i < n; i++)
    consputc(buf[i] & 0xff);

  return n + k;
}

//
// 输出一个字符串。
//
static int
printstr(char *s)
{
  int n = 0;

  if (s == 0)
    s = "(null)";
  for (; *s; s++) {
    consputc(*s & 0xff);
    n++;
  }
  return n;
}

//
// 格式化输出到控制台, 返回实际写出的字符数。
//
int
printf(char *fmt, ...)
{
  va_list ap;
  int i, n = 0;

  va_start(ap, fmt);
  for (i = 0; fmt[i] != '\0'; i++) {
    if (fmt[i] != '%') {
      consputc(fmt[i] & 0xff);
      n++;
      continue;
    }

    i++;
    switch (fmt[i]) {
    case 'd':
      // va_arg(ap, int) 取到 32 位实参, 先符号扩展到 64 位再打印。
      n += printint((uint64)(int64)va_arg(ap, int), 10, 1);
      break;
    case 'u':
      n += printint((uint64)va_arg(ap, uint32), 10, 0);
      break;
    case 'x':
      // 十六进制统一带小写 0x 前缀、无前导零。
      consputc('0');
      consputc('x');
      n += 2;
      n += printint((uint64)va_arg(ap, uint32), 16, 0);
      break;
    case 'p':
      consputc('0');
      consputc('x');
      n += 2;
      n += printint(va_arg(ap, uint64), 16, 0);
      break;
    case 'l':
      // 长整型修饰: %ld / %lu / %lx
      i++;
      if (fmt[i] == 'd') {
        n += printint(va_arg(ap, uint64), 10, 1);
      } else if (fmt[i] == 'u') {
        n += printint(va_arg(ap, uint64), 10, 0);
      } else if (fmt[i] == 'x') {
        consputc('0');
        consputc('x');
        n += 2;
        n += printint(va_arg(ap, uint64), 16, 0);
      } else {
        consputc('%');
        consputc('l');
        if (fmt[i] != '\0')
          consputc(fmt[i] & 0xff);
        n += 3;
      }
      break;
    case 'c':
      consputc(va_arg(ap, int) & 0xff);
      n++;
      break;
    case 's':
      n += printstr(va_arg(ap, char *));
      break;
    case '%':
      consputc('%');
      n++;
      break;
    case '\0':
      // 格式串以孤立 '%' 结尾: 结束解析。
      goto done;
    default:
      // 未知占位符: 原样输出, 便于发现格式错误。
      consputc('%');
      consputc(fmt[i] & 0xff);
      n += 2;
      break;
    }
  }

done:
  va_end(ap);
  return n;
}

//
// 不可恢复的内核错误: 打印信息后停住。
//
void
panic(char *s)
{
  printf("panic: %s\n", s);
  for (;;)
    ;
}
