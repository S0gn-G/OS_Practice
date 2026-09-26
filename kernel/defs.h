// number of elements in fixed-size array
#define NELEM(x) (sizeof(x) / sizeof((x)[0]))

// console.c
void            consoleinit(void);
void            consoleintr(int);
void            consputc(int);

// kalloc.c
void*           kalloc(void);
void            kfree(void*);
void            kinit(void);

// plic.c
void            plicinit(void);
int             plic_claim(void);
void            plic_complete(int);

// printk.c
int             printk(char*, ...) __attribute__ ((format (printf, 1, 2)));
void            panic(char*) __attribute__((noreturn));
void            printkinit(void);

// proc.c
int             cpuid(void);
void            proc_mapstacks(pagetable_t);
struct cpu*     mycpu(void);
struct proc*    myproc();
void            procinit(void);
void            procdump(void);

// string.c
void*           memset(void*, int, uint);

// syscall.c
void            syscall();

// trap.c
void            trapinit(void);
void            prepare_return(void);

// uart.c
void            uartinit(void);
void            uartintr(void);
void            uartputc_sync(int);

// vm.c
void            kvminit(void);
void            kvmmap(pagetable_t, uint64, uint64, uint64, int);
int             mappages(pagetable_t, uint64, uint64, uint64, int);
pte_t*          walk(pagetable_t, uint64, int);
