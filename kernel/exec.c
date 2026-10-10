#include "types.h"
#include "param.h"
#include "course_sid.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

//
// lab2: 磁盘驱动和文件系统要到 lab6 才有。现阶段由 Makefile 的 userimg
// 规则把用户程序编成"基址 0 的平铺二进制"，用 .incbin 嵌进内核 .rodata，
// 并在 _uprog_table 里登记 {start, end, name} —— 这就是本阶段的"文件系统"。
//
// 生成出来的 .rodata 布局(注意镜像数据就夹在表项中间，不是连续数组):
//
//   _uprog_table:
//     .quad _uprog_sh_start     # ┐ 表项头 16 字节
//     .quad _uprog_sh_end       # ┘
//     .asciz "sh"               #   名字(含结尾 '\0')
//     .balign 8                 #   8 字节对齐
//   _uprog_sh_start:
//     .incbin "user-flat/sh.bin"    # ← 镜像数据
//   _uprog_sh_end:
//     .balign 8                 #   下一项表头从这里开始
//     .quad _uprog_hi_start
//     ...
//     .quad 0                   # 哨兵
//     .quad 0
//
// 所以"下一项" = align8(本项 end)，不能按"16 + strlen(name) + 1"去推。
//
//
// 内嵌程序表表项(lab3 起扩展两个字段):
//   start/end  —— .bin 在内核里的地址范围
//   data_off   —— 可写段(数据段)起始的虚址偏移, 由 Makefile 生成的派生链接脚本
//                 在 .data 前插入页对齐保证; [0, data_off) 即代码段+只读数据段
//   img_end    —— ELF 映像末尾偏移(含 .bss; 取自 user.ld 的 end 符号, 因为
//                 平铺 .bin 不包含 .bss 的内容, 只靠文件长度无法覆盖 .bss)
// 注意: "下一项"仍 = align8(本项 end), 与表头有几个字段无关。
//
struct uprog {
  uint64 start;
  uint64 end;
  uint64 data_off;
  uint64 img_end;
  char name[];
};

extern struct uprog _uprog_table[];

static uint64 uprog_next(struct uprog* u) {
  return ((uint64)u->end + 7) & ~7UL;
}

//
// 比较表内程序名与 exec 传入的名字是否相同。
// 注意 sh 用 gets() 读命令行，而行尾的 '\n' 会被留在缓冲区里
// (sh.c 与 ulib.c 都是只读预置代码，改不了)，所以 want 尾部的
// 空白要一并吃掉再判结束。
//
static int nameeq(const char* tbl, const char* want) {
  int i = 0;

  while (tbl[i] != '\0' && tbl[i] == want[i])
    i++;
  if (tbl[i] != '\0')
    return 0;
  while (want[i] == ' ' || want[i] == '\t' || want[i] == '\r' || want[i] == '\n')
    i++;
  return want[i] == '\0';
}

//
// the implementation of the exec() system call
//
// 用户地址空间布局(与 user.ld 的 trampoline ABI 一致，程序从 0 链接):
//
//   [0, data_off)              代码段 + 只读数据段 R+X(严禁 W)
//   [data_off, sz)             数据段 + .bss       R+W
//   [sz, sz+G*PGSIZE)          guard 页(G = LAB3_GUARD_PAGES, 哑映射: 清 PTE_U)
//   [sz+G*PGSIZE, totalsz)     用户栈(USERSTACK 页)，sp 落在这里的顶端
//   TRAPFRAME/TRAMPOLINE       由 proc_pagetable() 布置在最高虚址
//
int kexec(char* path, char** argv) {
  struct uprog* u;
  struct proc* p = myproc();
  pagetable_t pagetable, oldpagetable;
  uint64 filesz, memsz, data_off, sz, totalsz, sp, off, oldsz;

  // lab2 阶段一律忽略 argv: exec 会把整个地址空间换掉，argv 字符串本来
  // 就躺在被换掉的旧映像里(xv6 的做法是先把它压到新栈上)；内嵌程序
  // 也都是 main(void)，不需要参数。argv == 0 也必须能用。
  (void)argv;

  // 按名字在内嵌程序表里查找，哨兵项 start == 0。
  for (u = _uprog_table; u->start != 0; u = (struct uprog*)uprog_next(u)) {
    if (nameeq(u->name, path))
      break;
  }
  if (u->start == 0)
    return -1;

  if ((pagetable = proc_pagetable(p)) == 0)
    return -1;

  filesz = u->end - u->start;                          // .bin 实际字节数
  memsz = (u->img_end > filesz) ? u->img_end : filesz; // 含 .bss 的映像大小
  data_off = u->data_off;
  sz = PGROUNDUP(memsz); // 映像占 [0, sz)

  // 1) 映像按段权限映射: 代码段(含只读数据) R+X —— 严禁写权限(承 badaccess
  //    TEST-3a), 数据段(含 .bss) R+W。uvmalloc 自带 PTE_R|PTE_U, 权限位随
  //    xperm 传入, 所以这里 PTE_X 与 PTE_W 分两次调用。
  if (data_off == 0 || data_off > sz) {
    // 段边界元数据缺失(理论上不会发生): 退化为整段 R+W+X, 保证系统仍可用
    if (uvmalloc(pagetable, 0, sz, PTE_X | PTE_W) == 0)
      goto bad;
  } else {
    if (uvmalloc(pagetable, 0, data_off, PTE_X) == 0)
      goto bad;
    if (sz > data_off && uvmalloc(pagetable, data_off, sz, PTE_W) == 0)
      goto bad;
  }

  // 逐页搬 .bin 的内容; 文件未覆盖的尾部(.bss)保持 kalloc 的零页即可。
  for (off = 0; off < filesz; off += PGSIZE) {
    uint64 n = (filesz - off < PGSIZE) ? (filesz - off) : PGSIZE;
    memmove((char*)walkaddr(pagetable, off), (char*)u->start + off, n);
  }

  // 2) 映像之后: LAB3_GUARD_PAGES 页 guard + USERSTACK 页用户栈。
  totalsz = sz + (LAB3_GUARD_PAGES + USERSTACK) * PGSIZE;
  if (uvmalloc(pagetable, sz, totalsz, PTE_W) == 0)
    goto bad;
  for (int i = 0; i < LAB3_GUARD_PAGES; i++)
    uvmclear(pagetable, sz + (uint64)i * PGSIZE); // guard 页对 U 态不可见
  sp = totalsz;                                   // 页对齐，自然满足 16 字节栈对齐

  // 3) 伪造第一现场。trapframe 是 kalloc 拿来的脏页，先整块清零。
  //    kernel_satp/kernel_sp/kernel_trap/kernel_hartid 四个头部字段由
  //    prepare_return() 在返回用户态之前填好，这里不碰。
  memset(p->trapframe, 0, sizeof(struct trapframe));
  p->trapframe->epc = 0; // 程序入口 = 加载基地址
  p->trapframe->sp = sp; // 用户栈顶

  safestrcpy(p->name, u->name, sizeof(p->name)); // 调试用(procdump、报错)

  // 4) 提交新地址空间，回收旧的。
  oldpagetable = p->pagetable;
  oldsz = p->sz;
  p->pagetable = pagetable;
  p->sz = totalsz;
  proc_freepagetable(oldpagetable, oldsz);

  return 0; // 进 trapframe->a0，即 main(argc, argv) 的 argc

bad:
  proc_freepagetable(pagetable, sz);
  return -1;
}
