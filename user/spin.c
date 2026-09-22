/* 预置支撑代码（请勿修改） —— 忙循环演示用户程序(lab2)
 * 用途:持续占用 CPU 运行,验证时钟中断下与 sh 的交互与并发。
 */
#include "kernel/types.h"
#include "user/user.h"

int main(void) {
  for (int i = 0; ; i++) {
    printf("spin %d\n", i);
    for (volatile int d = 0; d < 2000000; d++)
      ;
  }
}
