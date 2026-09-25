// number of elements in fixed-size array
#define NELEM(x) (sizeof(x) / sizeof((x)[0]))

// console.c
void            consoleinit(void);
void            consoleintr(int);
void            consputc(int);

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
struct cpu*     mycpu(void);
struct proc*    myproc();
void            procinit(void);
void            procdump(void);

// syscall.c
void            syscall();

// trap.c
void            trapinit(void);
void            prepare_return(void);

// uart.c
void            uartinit(void);
void            uartintr(void);
void            uartputc_sync(int);
