lab1 实现说明（启动与串口输出）
==============================

一、文件分工
  kernel/entry.S   阶段一：关中断（csrw mie,zero）、读 mhartid 只放行 BOOT_HART(0)、
                   按 LAB1_STACK_KB 计算 sp（sp = stack0 + (hartid+1)*LAB1_STACK_KB*1024），
                   从核进 park 循环（wfi），主核 call start。
  kernel/start.c   阶段二：M 态初始化 —— mstatus.MPP=S、mepc=main、satp=0、
                   medeleg/mideleg 委托、mie/sie 使能、PMP（pmpaddr0=0x3fffffffffffff,
                   pmpcfg0=0xf）、menvcfg.ADUE、tp=hartid，最后 mret 降级到 S 态。
                   stack0 为 LAB1_STACK_KB*1024*NCPU 字节、16 字节对齐（无硬编码 4096）。
  kernel/console.c 阶段三：uartputc_sync() 轮询 LSR 第 5 位（0x20，TX_IDLE）后写 THR；
                   consputc() 为其上层接口；consoleinit() 做 8N1 + 38.4K + FIFO 基本配置。
  kernel/printf.c  阶段四：%d/%s/%x/%%（另支持 %c/%u/%p/%ld/%lu/%lx），
                   除基取余 + 逆序输出；数值转换 fmtuint() 与 Banner 共用同一份实现。
  kernel/main.c    S 态主入口：按 LAB1_BANNER_PROTOCOL 输出学号 Banner（本学号为协议 2：
                   正文后追加 [chk=校验和]，校验和为行内字节 ASCII 累加、32 位回绕）。
  kernel/selftest.c 自测用例（printf 边界 + 协议展开），默认不编译。

二、从完整 xv6 删掉的内容（本实验用不到的子系统）
  kernel/uart.c（中断式 uartwrite/uartintr 及 sleeplock、proc 依赖）已删除，
  轮询输出 uartputc_sync() 按说明书放在 console.c；
  defs.h 只保留 console/printf 的接口；param.h 只保留 NCPU/BOOT_HART；
  main.c 不再调用 kinit/kvminit/procinit/trapinit/scheduler 等（这些属于后续实验）；
  Makefile 的 OBJS 同步去掉 uart.o。

三、构建与验证
  make                      # 默认构建（-Wall -Werror 下无编译告警）
  make qemu                 # 实跑，应输出：
                            #   OSLAB1 sid=2024302111292 mod97=0x52
                            #   [chk=2290]
                            # Ctrl-a 松开后按 x 退出 QEMU

  # 逐字节比对实跑输出与 expect_banner.txt
  timeout 5 qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
      -nographic > tests/kernel_output.txt 2>/dev/null
  cmp tests/kernel_output.txt expect_banner.txt && echo "[OK]"

  python3 check_expect.py 2024302111292            # 课程格式自检（[ok]）
  python3 tests/check_protocol.py 2024302111292    # 独立重算校验和并核对协议

  # 自测用例（printf 边界 + 协议边界），只跑用例、不打印 Banner
  make clean && make LAB1_SELFTEST=1
  timeout 5 qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
      -nographic > /tmp/st.txt 2>/dev/null
  cmp /tmp/st.txt tests/expect_selftest.txt && echo "[OK] selftest"
  用例覆盖：0、负数、INT_MAX/INT_MIN、%x 的 0/0x48/0xdeadbeef/UINT_MAX（小写、无前导零）、
  空串、NULL 串、%c、%%、未知占位符、printf 返回字符数、协议 1 展开、协议 2 校验和。

四、排错（说明书 §3）
  qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -nographic \
      -d int -D int.log
  无输出时先看 int.log 的同步异常：cause=1 查 PMP；cause=2 查 S 态是否残留 M 态指令；
  CPU 100% 则多为 LSR 轮询条件写反或从核自旋逻辑有误。
