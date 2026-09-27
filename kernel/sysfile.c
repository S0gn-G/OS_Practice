//
// lab2 的 sysfile.c。原版 xv6 这里全是文件系统相关的系统调用
// (namei/readi/begin_op、struct file 与 devsw 那一套)，而 lab2 还没有
// 磁盘驱动、inode、日志(lab6 才有)，所以这里只有两类东西:
//
//  1) 现阶段真正要用的三个 —— read / write / exec。
//     没有 fd 表(proc.h 里 ofile[] 仍是注释状态)，用最朴素的 fd 约定:
//         fd 0        控制台输入
//         fd 1、2     控制台输出
//         其余        -1
//     等 lab6 有了 struct file 与 devsw，这三个会退化成
//     argfd() + fileread()/filewrite() 的薄壳，调用链不用改。
//
//  2) 其余完全依赖文件系统的若干个 —— 现阶段一律"确定失败"返回 -1。
//     规范(能力目标 §2.1)要求未实现的调用返回确定错误、不得 panic;
//     把它们显式列出来而不是留在分发表外，一是让日志干净(不走
//     "unknown sys call" 那条 printk)，二是给 lab6 留好落点。
//
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

// 控制台 fd 约定(lab2 临时)
#define CONSOLE_IN  0
#define CONSOLE_OUT 1
#define CONSOLE_ERR 2

//
// read(fd, buf, n) —— 只支持标准输入。
//
uint64 sys_read(void) {
  int fd, n;
  uint64 buf;

  argint(0, &fd);
  argaddr(1, &buf);
  argint(2, &n);

  if (fd != CONSOLE_IN)
    return -1;

  return consoleread(buf, n);
}

//
// write(fd, buf, n) —— 只支持标准输出/标准错误。
//
uint64 sys_write(void) {
  int fd, n;
  uint64 buf;

  argint(0, &fd);
  argaddr(1, &buf);
  argint(2, &n);

  if (fd != CONSOLE_OUT && fd != CONSOLE_ERR)
    return -1;

  return consolewrite(buf, n);
}

//
// exec(name, argv) —— 名字在用户空间，Sv39 下内核页表没有映射用户地址，
// 必须先 copyinstr 拷进内核缓冲；argv 在 lab2 一律忽略(能力目标 §2.3)。
//
uint64 sys_exec(void) {
  char name[MAXPATH];

  if (argstr(0, name, sizeof(name)) < 0)
    return -1;

  return kexec(name, 0);
}

//
// ---- 以下都依赖文件系统，lab6 之前统一返回 -1 ----
// (sys_pipe 在 xv6 里也放在这个文件，它依赖 struct file + 环形缓冲)
//
uint64 sys_open(void) {
  return -1;
}

uint64 sys_close(void) {
  return -1;
}

uint64 sys_dup(void) {
  return -1;
}

uint64 sys_fstat(void) {
  return -1;
}

uint64 sys_link(void) {
  return -1;
}

uint64 sys_unlink(void) {
  return -1;
}

uint64 sys_mknod(void) {
  return -1;
}

uint64 sys_mkdir(void) {
  return -1;
}

uint64 sys_chdir(void) {
  return -1;
}

uint64 sys_pipe(void) {
  return -1;
}

uint64 sys_sync(void) {
  return -1;
}
