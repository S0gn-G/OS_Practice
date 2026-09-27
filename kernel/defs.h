// number of elements in fixed-size array
#define NELEM(x) (sizeof(x) / sizeof((x)[0]))

// clang-format off
struct context;

// console.c
void            consoleinit(void);
void            consoleintr(int);
void            consputc(int);

// exec.c
int             kexec(char*, char**);

// kalloc.c
void*           kalloc(void);
void            kfree(void*);
void            kinit(void);

// plic.c
void            plicinit(void);
void            plicinithart(void);
int             plic_claim(void);
void            plic_complete(int);

// printk.c
void            panic(char*) __attribute__((noreturn));
int             printk(char*, ...) __attribute__ ((format (printf, 1, 2)));
void            printkinit(void);

// proc.c
int             cpuid(void);
void            kexit(int);
int             kfork(void);
int             killed(struct proc*);
int             kwait(uint64);
struct cpu*     mycpu(void);
struct proc*    myproc();
void            procdump(void);
void            procinit(void);
void            proc_freepagetable(pagetable_t, uint64);
void            proc_mapstacks(pagetable_t);
pagetable_t     proc_pagetable(struct proc*);
void            sched(void);
void            scheduler(void) __attribute__((noreturn));
void            sleep(void);
void            sleep_prepare(void*);
void            userinit(void);
void            wakeup(void*);

// string.c
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);

// swtch.S
void            swtch(struct context*, struct context*);

// syscall.c
void            argint(int, int*);
void            argaddr(int, uint64*);
void            syscall();

// trap.c
void            prepare_return(void);
void            trapinit(void);
void            trapinithart(void);

// uart.c
void            uartinit(void);
void            uartintr(void);
void            uartputc_sync(int);

// vm.c
int             copyout(pagetable_t, uint64, uint64, char*, uint64);
int             ismapped(pagetable_t, uint64);
void            kvminit(void);
void            kvminithart(void);
void            kvmmap(pagetable_t, uint64, uint64, uint64, int);
int             mappages(pagetable_t, uint64, uint64, uint64, int);
uint64          uvmalloc(pagetable_t, uint64, uint64, int);
void            uvmclear(pagetable_t, uint64);
int             uvmcopy(pagetable_t, pagetable_t, uint64);
pagetable_t     uvmcreate(void);
uint64          uvmdealloc(pagetable_t, uint64, uint64);
void            uvmfree(pagetable_t, uint64);
void            uvmunmap(pagetable_t, uint64, uint64, int);
uint64          vmfault(pagetable_t, uint64, uint64, int);
pte_t*          walk(pagetable_t, uint64, int);
uint64          walkaddr(pagetable_t, uint64);
