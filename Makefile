CC   = riscv64-unknown-elf-gcc
LD   = riscv64-unknown-elf-ld
CFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc -fno-stack-protector -fno-pie
QEMU = qemu-system-riscv64

K=kernel
U=user

OBJS = \
	$K/entry.o \
	$K/start.o \
	$K/console.o \
	$K/printk.o \
	$K/uart.o \
	$K/kalloc.o \
	$K/string.o \
	$K/main.o \
	$K/vm.o \
	$K/proc.o \
	$K/swtch.o \
	$K/trampoline.o \
	$K/trap.o \
	$K/syscall.o \
	$K/exec.o \
	$K/kernelvec.o \
	$K/plic.o \
	$K/userimg.o

$K/kernel: $(OBJS) $K/kernel.ld
	$(LD) -T $K/kernel.ld -o $@ $(OBJS)

$K/%.o: $K/%.c $K/riscv.h $K/course_sid.h
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

$K/%.o: $K/%.S
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

# 验收环境固定(排雷环节禁止改动本行以下内容)
qemu: $K/kernel
	$(QEMU) -machine virt -bios none -kernel $K/kernel -nographic

clean:
	rm -f */*.o $K/kernel

# 预置构建配置 —— lab2 Makefile 升级集成片段(请勿修改本规则本身;
# 合并方式:把下面全部规则并入你的 Makefile,并把 kernel/userimg.o
# 加进你的 OBJS。三固定项(make kernel / make qemu / course_sid.h
# 依赖)保持不变。
# ⚠ 历史合并常见注意事项(已在本文件内修复,合并时整段引用即可):
#   1) OBJCOPY 变量:基线 Makefile 未定义,本文件已自带定义;
#   2) UFLAGS 需含 -I. :用户程序 #include "kernel/types.h" 从树根解析。)
#
# 设计说明:lab2 阶段尚未实现磁盘与文件系统(将在 lab6 中系统实现)。
# 用户程序由本规则链编译为平铺二进制,经 userimg.S 内嵌进内核镜像;
# 你的 sys_exec 从内嵌程序表按名字查找并加载(表格式见下方注释)。

UPROGS = sh hi spin

OBJCOPY ?= riscv64-unknown-elf-objcopy

UFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc \
         -fno-stack-protector -fno-pie -I.

# usys 存根由 perl 脚本自动生成(课程预置支撑脚本，请勿修改)
$U/usys.S: $U/usys.pl
	perl $U/usys.pl > $U/usys.S

# 用户程序 → 链接(基址 0)→ 平铺二进制
user-flat/%.bin: $U/%.c $U/user.h $U/ulib.c $U/printf.c $U/usys.S
	mkdir -p user-flat
	$(CC) $(UFLAGS) -Iuser -c $U/ulib.c   -o user-flat/ulib.o
	$(CC) $(UFLAGS) -Iuser -c $U/printf.c -o user-flat/printf.o
	$(CC) $(UFLAGS) -Iuser -c $U/usys.S   -o user-flat/usys.o
	$(CC) $(UFLAGS) -Iuser -c $<            -o user-flat/$*.o
	@if [ -f $U/common.c ]; then \
	  $(CC) $(UFLAGS) -Iuser -c $U/common.c -o user-flat/common.o; \
	  EXTRA_OBJS=user-flat/common.o; \
	else EXTRA_OBJS=; fi; \
	$(LD) -T $U/user.ld -o user-flat/$*.elf \
	    user-flat/$*.o $$EXTRA_OBJS user-flat/ulib.o user-flat/printf.o user-flat/usys.o
	$(OBJCOPY) -O binary user-flat/$*.elf $@

# 内嵌程序表(ABI,勿改):
#   _uprog_table: 每项 = .quad start; .quad end; .asciz "name";
#   以 .quad 0; .quad 0 结尾。内核侧按表遍历。
$K/userimg.S: Makefile $(addprefix user-flat/,$(UPROGS:=.bin))
	printf '.section .rodata\n .global _uprog_table\n_uprog_table:\n' > $@.tmp
	for p in $(UPROGS); do \
	  printf ' .quad _uprog_%s_start\n .quad _uprog_%s_end\n .asciz "%s"\n .balign 8\n' \
	    $$p $$p $$p >> $@.tmp; \
	  printf ' .global _uprog_%s_start\n_uprog_%s_start:\n .incbin "user-flat/%s.bin"\n .global _uprog_%s_end\n_uprog_%s_end:\n .balign 8\n' \
	    $$p $$p $$p $$p $$p >> $@.tmp; \
	done
	printf ' .quad 0\n .quad 0\n' >> $@.tmp
	mv $@.tmp $@

clean-extra:
	rm -rf user-flat $K/userimg.S $U/usys.S