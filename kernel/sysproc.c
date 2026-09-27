#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

uint64 sys_exit(void) {
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64 sys_getpid(void) {
  return myproc()->pid;
}

uint64 sys_fork(void) {
  return kfork();
}

uint64 sys_wait(void) {
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}
