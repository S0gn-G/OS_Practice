#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
static uint64 (*syscalls[])(void) = {
  // clang-format off
  // [SYS_fork]    = sys_fork,
  // [SYS_exit]    = sys_exit,
  // [SYS_wait]    = sys_wait,
  // [SYS_pipe]    = sys_pipe,
  // [SYS_read]    = sys_read,
  // [SYS_kill]    = sys_kill,
  // [SYS_exec]    = sys_exec,
  // [SYS_fstat]   = sys_fstat,
  // [SYS_chdir]   = sys_chdir,
  // [SYS_dup]     = sys_dup,
  // [SYS_getpid]  = sys_getpid,
  // [SYS_sbrk]    = sys_sbrk,
  // [SYS_pause]   = sys_pause,
  // [SYS_uptime]  = sys_uptime,
  // [SYS_open]    = sys_open,
  // [SYS_write]   = sys_write,
  // [SYS_mknod]   = sys_mknod,
  // [SYS_unlink]  = sys_unlink,
  // [SYS_link]    = sys_link,
  // [SYS_mkdir]   = sys_mkdir,
  // [SYS_close]   = sys_close,
  // [SYS_sync]    = sys_sync,
  // clang-format on
};

void syscall(void) {
  int num;
  struct proc* p = myproc();

  num = p->trapframe->a7;
  if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    p->trapframe->a0 = syscalls[num]();
  } else {
    printk("%d %s: unknown sys call %d\n", p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
