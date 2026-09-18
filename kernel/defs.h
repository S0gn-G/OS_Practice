// console.c
void    consoleinit(void);
void    consputc(int);

// main.c
void    main(void);

// printk.c
void    printkinit(void);
int     printk(char*, ...) __attribute__ ((format (printf, 1, 2)));

// uart.c
void    uartinit(void);
void    uartputc_sync(int);
