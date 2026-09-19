// lab1 内核接口声明。
// 只保留本实验实际存在的模块(console / uart / printf),
// 其余子系统(bio、exec、fs、proc、vm...)的声明随后续实验引入。

// console.c
void consoleinit(void);
void consputc(int);
void uartputc_sync(int);

// printf.c
int  printf(char*, ...);
void panic(char*) __attribute__((noreturn));

// printf.c 导出的数值->文本转换(供 Banner 与 printf 共用同一份实现)
int  fmtuint(uint64, int, char*);

// selftest.c (仅 make LAB1_SELFTEST=1 时链入)
int  selftest(void);
