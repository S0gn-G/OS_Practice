/* 预置支撑代码（请勿修改） —— 最小 shell(lab2)
 * 与 xv6 原版 sh 的差别:无参数解析、无重定向、无 cd——留待后续实验
 * 逐步实现支撑。它只依赖 fork/exec/wait/read/write 系统调用。
 * exec 的 ABI 是完整 xv6 签名 exec(name, argv); lab2 的内核实现
 * 允许暂时忽略 argv(内嵌程序表里的程序均无参数)——完整参数解析将在 lab5 中完善。
 */
#include "kernel/types.h"
#include "user/user.h"

int main(void) {
  static char buf[64];

  while (1) {
    printf("sh> ");
    gets(buf, sizeof(buf));
    if (buf[0] == '\0')          /* 空行/EOF */
      continue;

    int pid = fork();
    if (pid < 0) {
      printf("fork failed\n");
      continue;
    }
    if (pid == 0) {
      char *argv[] = { buf, 0 };
      exec(buf, argv);
      printf("exec %s failed\n", buf);
      exit(1);
    }
    wait(0);
  }
}
