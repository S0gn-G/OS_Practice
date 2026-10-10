# lab3 设计笔记（SV39 三级页表与物理内存管理）

> 学号 2024302111292 · 个性化参数：`LAB3_ALLOC_ORDER=2`（中点）、`LAB3_GUARD_PAGES=1`、`LAB3_MEMCAP_DELTA=12`
> 基线：`lab3-start`（增量包 v2 合并后）　交付：`lab3-submit`
> 本文按说明书四阶段组织，每条设计决策给出**实现、不变式、实测证据**；附录收录已从树中删除的临时自检源码与输出，便于现场复现。

---

## 一、总览

| 阶段 | 主要改动 | 提交 |
| --- | --- | --- |
| 一 物理页分配器 | `kernel/kalloc.c` 重写（位图 + 中点顺序 + 配额 + 统计接口），`kernel/defs.h` | `f53b8e9` |
| 二 页表遍历与回滚 | `kernel/vm.c`：`mappages` 三段式 + `prune_empty` | `34b9359` |
| 三 内核页表与使能分页 | 功能在 lab2 已就位；本阶段补 `MENVCFG_ADUE`（A/D 硬件置位） | `34b9359` |
| 四 用户地址空间 | `kernel/exec.c`（段级权限、`LAB3_GUARD_PAGES`）、`kernel/vm.c`（`dump_pagetable`）、`kernel/proc.c`（dump 调用点）、`Makefile`（从 ELF 段表算段边界、表项扩展、`make dump`、测试纳入）、`user/user.ld`（段页对齐） | 本次交付 |

实测基线数字（本机、`-m 128M`）：受管起点 `PGROUNDUP(end)=0x80024000`，`total=32732` 页，`usable=32720` 页（= total − 12）。

---

## 二、阶段一：物理页分配器

### 2.1 数据结构选型（说明书要求对比时间与空间开销）

| 方案 | 元数据开销 | 释放复杂度 | 分配复杂度（含顺序语义） | memcap（数万页批量回收） |
| --- | --- | --- | --- | --- |
| 隐式空闲链表（lab2 的 LIFO） | 0 | O(1) | O(1)，但**无法表达升序/降序/中点** | 快，但不满足参数 |
| 有序空闲链表 | 0 | 按地址插入 **O(n)** | O(1) | 约 3.2 万次释放 × O(n) 指针走查 → 说明书点名的 $O(n^2)$，QEMU 下可达数分钟，易与死锁混淆 |
| **位图（选型）** | 1 bit/页；128MB → **4KB**（512 个 `uint64`，静态放 `.bss`） | **O(1)** | 游标/提示位摊销 **O(1)** | 满足；memcap 实测 2029 轮、数十秒内完成 |

位图放 `.bss`（随内核镜像一起被 `end` 记账），避免"元数据要从被管理内存里分配"的自举问题；顺序语义退化为"扫描方向"的选择。

### 2.2 配额记账（`LAB3_MEMCAP_DELTA=12`）

- `managed_start = PGROUNDUP(end)`，`total = (PHYSTOP − managed_start)/PGSIZE`，`usable = total − 12`；
- `kinit` 只把前 `usable` 页置为空闲，**顶部 12 页从不进位图**——上限是硬保证，`nfree` 归零即 OOM；
- 不变量：`nfree == 位图中 1 的个数`、`nfree ≤ usable`、`kalloc` 返回值要么为 0 要么是 4KB 对齐且全 0 的页。

### 2.3 中点顺序（`LAB3_ALLOC_ORDER=2`）的精确定义

以 `mid = usable/2` 为起点向两侧扩散，两个游标**只前进**，序列为

```
mid, mid−1, mid+1, mid−2, mid+2, …
```

每轮比较两游标到 `mid` 的距离，先试更近的一侧（等距优先低地址，保证首取正是 `mid`）；某方向扫到边界即报废，另一侧独自继续；两游标都报废而 `nfree > 0`（说明释放出的空洞落在游标身后）时，把游标重置回 `mid` 重试一轮（**回绕**）。

> 可观测取舍：因为游标只前进，刚释放、位于游标身后的页在回绕前不会被再次选中。实测抽样（阶段一自检）：连续取到 `mid+4 / mid−5 / mid+5`，正好反映该行为。

### 2.4 `kalloc` 清零与 `kfree` 填垃圾

| 行为 | 目的 |
| --- | --- |
| `kalloc` 返回前 `memset(pa, 0, PGSIZE)` | **数据安全隔离**：新主人（新进程、新页表页）不得看到上一任内容；同时让"新分配页已清零"成为分配器级不变量 |
| `kfree` 前 `memset(pa, 0x01, PGSIZE)` | **悬挂引用排查**：use-after-free 的读者会立刻看到 0x0101… 而不是旧数据 |

`kfree` 对非法地址/未对齐/**重复释放**一律 `panic`（不变量违反）；`kalloc` 在 `nfree==0` 时返回空指针，绝不 panic。

### 2.5 只读统计接口

`kalloc_nfree()` / `kalloc_total()` / `kalloc_usable()`：自检、阶段二的"回滚后空闲页复原"断言、现场演示都用它，不提供写入口。

### 2.6 一个构建期坑（记录备查）

`kalloc.c` 里不能用 `__builtin_ctzl/clzl`：`-march=rv64gc` 无 Zbb，GCC 会把内建降级为 libgcc 的 `__ctzdi2/__clzdi2`，而本工程用裸 `ld -nostdlib` 链接，直接报未定义符号（本次实现时踩到过）。现改为"按字跳过 + 字内逐位判"。

### 2.7 阶段一实测（自检输出，源码见附录 A）

```
SELFTEST begin usable=32720 total=32732 delta=12
SELFTEST nfree after alloc8 = 32712 (expect 32712)     ← 分配计数
SELFTEST nfree after free8 = 32720 (expect 32720)      ← 释放守恒
SELFTEST order[0] pa=0x0000000084010000                ← mid+4
SELFTEST order[1] pa=0x0000000084007000                ← mid−5(游标身后刚释放的页不回选)
SELFTEST order[2] pa=0x0000000084011000                ← mid+5
SELFTEST exhaust cnt=32720 usable=32720 nfree=0 maxpa=0x0000000087ff3000
SELFTEST after reinit nfree=32720 (expect 32720)
SELFTEST PASS
```

`maxpa = 0x87ff3000 < PHYSTOP − 12×PGSIZE = 0x87ff4000`，证明**顶部保留配额从未被发出**；耗尽计数恰等于 `usable`。

---

## 三、阶段二：页表遍历与映射回滚

### 3.1 `walk` 的语义

- 三级页索引：`PXSHIFT(level) = 12 + 9×level`、`PXMASK = 0x1FF`、`PX(level,va)`；
- `walk(pt, va, alloc)` 返回**叶子 PTE 的地址**而不是物理地址：调用方需要"读-改-写"该表项（置权限、清无效、判 `PTE_V`），拿物理地址做不到；
- 中间级与叶子的本质区别是标志位：**R/W/X 全 0 即非叶**（指向下一级页表），任一置位即叶子；`freewalk` 的递归判据同此；
- `va >= MAXVA` 视为内核契约违反 → `panic`；用户可控地址在进入 `walk` 之前都已做过范围校验（`walkaddr` 查 `MAXVA`、`copyin/copyout` 查 `MAXVA`、`mappages` 查 `va+size`），因此这条 panic 不可能被用户触发。

### 3.2 `mappages` 三段式与回滚不变式（本实验核心考点）

```
① 前置校验：对齐 / size>0 作为契约 panic；va+size 溢出或 > MAXVA → -1
② 建立：逐页 walk(alloc=1) 写 PTE = PA2PTE(pa)|perm|PTE_V；任一页失败 → ③
③ 回滚：
   3a 清掉本次已写入的 PTE（不含失败页），**不释放调用方的物理页**
   3b 回收本次变空的中间级页表页（prune_empty：自底向上，先清父项再 kfree 子表）
   3c 返回 -1
```

**三条不变式（写死在函数头注释里）**

1. 成功 ⇔ 区间内每页都有 `V|perm` 的叶子 PTE；
2. 失败 ⇔ 区间内没有任何由本次调用建立的映射，且本次申请或因此变空的页表页全部归还（`nfree` 与调用前相等）；
3. 调用方传入的物理页 `pa` 恒归调用方所有——`mappages` 在成功与失败路径下都不释放它。

### 3.3 释放职责边界

| 函数 | 释放数据页 | 释放页表页 | 说明 |
| --- | --- | --- | --- |
| `mappages` | 否（任何路径） | 是（仅失败时，仅回收变空的） | 阶段二核心 |
| `uvmalloc` | 是（失败时释放本批新页） | 否 | 交给 `mappages` 回滚 |
| `uvmunmap` | 由 `do_free` 决定 | **否**（统一留给 `freewalk`） | 见 3.6 |
| `uvmcopy` / `exec` / `kfork` | 是（各自失败分支） | 否 | 现有逻辑不变 |
| `freewalk` | — | 是（整树） | 进程终结 |

### 3.4 为什么调用方一行都不用改（"回滚会不会和自回滚代码打架"）

关键事实：`uvmalloc`、`uvmcopy`、`vmfault`、`proc_pagetable` 都是**单页调用** `mappages`（循环内每次 `PGSIZE`），只有 `kvmmap` 用变长区间（失败即 panic）。于是：

- 一次失败的单页 `mappages` 自己写了 0 个 PTE，回滚范围为空（只剪掉它刚新建的空页表页）；
- 而调用方在失败分支回收的是**先前若干次成功调用**建好的映射（`kfree(mem)`、`uvmdealloc`、`uvmunmap`），两批对象不重叠；
- 回滚后调用方的清理天然幂等：`uvmunmap` 对 `walk()==0` 或 `*pte & PTE_V == 0` 都是 `continue`；`freewalk` 只递归有效非叶项。

若反过来让 `mappages` 顺带 `kfree(pa)`，则 `uvmalloc` 的 `kfree(mem)`/`uvmdealloc` 会双重释放（撞 `panic("kfree: double free")`），更严重的是 `proc_pagetable` 会把 **trampoline 内核代码页**与 `p->trapframe` 释放掉。因此 `pa` 归调用方是本设计不可动摇的边界。

### 3.5 重复映射：保持 `panic("mappages: remap")`

说明书的三种允许策略（覆盖/错误码/停机）中选**停机**：对已有有效映射的 VA 再次映射属于内核 bug（会泄漏旧物理页或破坏"页表=唯一真相"），应尽早暴露；且用户可控路径经核查到不了这里（`vmfault` 先用 `ismapped` 挡掉、`uvmalloc` 用 `PGROUNDUP(oldsz)` 起算、`exec` 用全新页表）。

### 3.6 `uvmunmap` 不做剪枝

与 xv6 一致：解映射只清叶子 PTE（按 `do_free` 释放数据页），页表页统一由 `freewalk` 在进程终结时整树回收。代价是 `uvmalloc`/`sbrk` 失败路径会残留几个"空但被引用"的页表页（进程结束才回收），因此**从 `uvmalloc` 层观测 `nfree` 不会严格复原**；按说明书测法**直接对 `mappages` 测**则严格复原（见 3.7）。取舍已记录，若将来要求两级都严格复原，只需给 `uvmunmap` 复用同一套 `prune_empty`。

### 3.7 TLB 刷新时机

- 修改**未生效**页表（`satp` 未指向它）：不需要刷新（exec 构建新表属此类）；
- 修改**生效中**页表：使用新映射前必须 `sfence.vma`；
- 现有实现已在关键点刷新：`kvminithart` 写 `satp` 前后各一次、`uservec` 切换内核表前后各一次、`userret` 切回用户表前后各一次——因此 `sbrk` 在用户页表上做的映射/解映射由**返回用户态那一次 flush 兜底**；
- 故 `mappages` 内部不刷新，刷新责任在调用方；多核下 `sfence.vma` 只刷当前核，本实验单核，作为思考题留档（若多核则需核间 TLB shootdown）。

### 3.8 阶段二实测（自检输出，源码见附录 B）

```
SELFTEST2 A map3+walk: ok nfree=32555 (expect 32555)          ← 成功路径 + walk 反查 pa/perm
SELFTEST2 B exhaust: consumed=32551 left=3
SELFTEST2 B bigmap rc=-1 nfree=3 (expect -1/3) valid_pte=0 root=pruned
SELFTEST2 B restore nfree=32555 (expect 32555)
SELFTEST2 PASS
SELFTEST2 C remap: expecting panic(mappages: remap)
panic: mappages: remap                                         ← 重复映射策略生效
```

B 组场景：只剩 3 页空闲时请求映射 2048 页（需 1 个 L1 + 4 个 L0 页表页），映射到第 1024 页时分配失败 → `rc=-1`、`nfree` 严格回到 3、区间内 0 个有效 PTE、**根项被剪掉（`pruned`）**，即"已写的 1024 个 PTE + 本次新建的 3 张页表页"全部回滚干净。

---

## 四、阶段三：内核页表与使能分页

### 4.1 映射清单与权限依据

| 区域 | 虚拟地址 | 权限 | 依据 |
| --- | --- | --- | --- |
| UART0 | `0x10000000` | R+W | 内核驱动直接读写串口寄存器（必需项） |
| PLIC | `0x0c000000` + 64MB | R+W | 中断 claim/complete 直接访存（必需项） |
| 内核代码段 | `KERNBASE..etext` | R+X | 可执行、不可写；`etext` 由 `kernel.ld` 导出，故能细分（见 4.3） |
| 内核数据段 + 全部可用物理内存 | `etext..PHYSTOP` | R+W | **必须映射到 PHYSTOP 而非镜像末尾**：启用分页后内核要直接读写 `kalloc` 发出去的一切物理页（页表页、用户页、内核栈） |
| TRAMPOLINE | `MAXVA − PGSIZE` | R+X | 与用户页表**同址**映射，`satp` 切换瞬间 PC 取指连续 |
| 每进程内核栈 | `KSTACK(i)` | R+W | `memlayout.h` 的高地址布局，与 lab0 图纸一致；下方留无效保护页 |
| CLINT | — | 不映射 | **不访问**：时钟走 `rdtime`/`stimecmp` CSR（`start.c` 的 `timerinit`、`trap.c` 的 `clockintr`），全仓库 grep 无 `CLINT`/`mtimecmp` 访存 |
| VIRTIO0 | — | 不映射 | lab6 才有磁盘驱动 |

### 4.2 `satp` 与 TLB

`satp = (8 << 60) | (根页表物理地址 >> 12)`（`SATP_SV39 | PPN`）；`kvminithart()` 写 `satp` 前后各执行一次 `sfence.vma`（写前保证之前的页表内存写入对 MMU 可见，写后清掉陈旧表项）。

### 4.3 两处命名/告警的说明

- **`etext`**：说明书假设"增量包 `kernel.ld` 未导出 `etext`，故本阶段只能统一 RWX 过渡"。本工程的 `kernel.ld` 已导出 `etext`，因此实现了 text `R+X` / data `R+W` 的段级拆分，**优于**说明书要求的过渡方案；
- **`kernel_end`**：课程基线链接脚本用 `kernel_end` 标记内核末尾，lab2 重写链接脚本后改为 `PROVIDE(end = .)`。本实验沿用 `end`（`kalloc.c` 的 `extern char end[]`），与说明书点名的符号名不同，属已知偏差；二者语义等价，改回别名即可兼容；
- **`ld` 的 `LOAD segment with RWX permissions` 警告**：属 ELF program header 层面的权限标记，与页表权限无关，内核页表里 text/data 权限已分离。

### 4.4 A/D 位管理

`start.c` 在 M 态打开 `menvcfg.ADUE`（`w_menvcfg(r_menvcfg() | MENVCFG_ADUE)`），声明**A/D 由硬件置位**；若平台不实现 Svadu 则该位为 WARL-0，无副作用。按 `dump_pagetable-ABI.md`，A/D 不在 perm 四位中输出。

### 4.5 验证

每次 `make` 后实跑：banner（协议 2：正文 + `[chk=2290]`）→ `sh>` → `hi`/`spin` 正常；lab2 的 `inject_uart.py` 回归全绿（见 6.4）。

---

## 五、阶段四：用户地址空间

### 5.1 布局（对照 lab0 地址空间快照图）

| 区间 | 权限 | 说明 |
| --- | --- | --- |
| `[0, data_off)` | `r-xu` | 代码段 + 只读数据段，**严禁 W**（badaccess TEST-3a 的考点） |
| `[data_off, sz)` | `rw-u` | 数据段 + `.bss` |
| `[sz, sz+G·4K)` | `rw--` | guard 页（G = `LAB3_GUARD_PAGES` = 1），**哑映射**：已建映射但清掉 `PTE_U` |
| `[sz+G·4K, totalsz)` | `rw-u` | 用户栈（`USERSTACK` = 1 页），`sp = totalsz` |
| 堆（`sbrk` 增长） | `rw-u` | 位于栈之上，向高地址增长；`p->sz` 即堆顶 |
| `TRAPFRAME` | `rw--` | 每进程一页，`TRAPFRAME = TRAMPOLINE − PGSIZE` |
| `TRAMPOLINE` | `r-x-` | 与内核页表同址同物理页 |

`fork` 深拷贝时对未映射页 `continue` 跳过；`TRAPFRAME`/`TRAMPOLINE` 不随进程数据复制（前者是每进程独立页但由 `proc_pagetable` 固定映射、后者是全系统共享的同一物理页，复制既无意义也会破坏 `uservec`/`userret` 的同址假设）。

### 5.2 段级权限数据链路（三步）

1. **构建期准备可分的段布局**：`user/user.ld` 在 `.data` 之前放 `. = ALIGN(0x1000);`，使"代码 + 只读数据"与"可写数据 + `.bss`"落在不同页。这样即使程序小于 4KB，页粒度的 R+X / R+W 也不会互相牵制。
   > 与说明书的偏差（如实记录）：说明书建议"预置 `user.ld` 不可直接修改，改为在构建脚本里生成派生链接脚本"。本工程直接在 `user.ld` 内完成页对齐（该文件已改为本地版本），**段边界元数据则完全不依赖链接脚本符号**，由 Makefile 从 ELF 段表计算，因此链接脚本是否导出符号都不影响内核。
2. **传递进内核**：Makefile 用 `riscv64-unknown-elf-size -A`（输出即十进制）对每个 `user-flat/%.elf` 计算
   - `data_off = min(addr(.data), addr(.bss))`——可写段起始；两者都不存在（纯代码程序）时取映像末尾向上页对齐，内核侧即得到"整段 R+X、无数据段"；
   - `img_end = max(addr+size) over 可分配段`——含 `.bss` 的映像末尾。
   两者写入内嵌程序表表项 `{start,end,data_off,img_end,name}`（"下一项"仍 = `align8(本项 end)`，遍历逻辑不变）。
   若取不到或边界不自洽（`data_off` 未页对齐、`data_off > img_end`），**make 直接失败**（`userimg: 无法从 … 提取段边界`），不再退化成默认值 0。
3. **装载时按页赋权**：`kexec` 分两段 `uvmalloc`——`[0,data_off)` 传 `PTE_X`（`uvmalloc` 自带 `R|U`，故得 `r-xu`），`[data_off,sz)` 传 `PTE_W`（得 `rw-u`）。

实测（本次构建）：`sh` → `data_off=4096`、`img_end=4160`（`.bss` 64B 在 0x1000）；`hi`/`spin` 等无数据段的程序 → `4096 / 4096`，数据段为空。dump 中 `sh` 的映像为 `LEAF 0x0 perm=r-xu` + `LEAF 0x1000 perm=rw-u`。

> **顺带修正的一个隐患**：平铺 `.bin` 不含 `.bss` 内容（NOBITS），仅用文件长度算映像大小会少算。此前 `sh` 的 `.bin` 只有 1793 字节而 `.bss` 末尾在 `0x1040`，旧实现 `sz=PGROUNDUP(1793)=0x1000`，`.bss` 尾部 `[0x1000,0x1040)` 与**保护页重叠**（当时被 `vmfault` 的按需补页掩盖）。现在 `memsz` 取 `max(filesz, img_end)`，`sz=0x2000`，保护页落到 `0x2000`，重叠消失。

> **一次回归教训**：把页对齐移入 `user.ld` 时若同时丢掉边界元数据（例如只保留 `ALIGN`、而 `end` 被写成无人引用的 `PROVIDE(end = .)`），会出现"映像算小 → 保护页压到 `.bss` 上 → 用户写全局变量被当成越界杀进程"，`sh` 作为 initproc 被杀还会 `panic: init exiting`。现在的做法不再依赖符号，并在构建期硬失败，堵住了这条静默降级路径。

### 5.3 guard 页的权限配置与异常判定路径

- 保护页**分配并映射了实际物理页**，但 `uvmclear` 清掉 `PTE_U`（哑映射方案）：对 U 态不可见，对 S 态可见；
- 用户越界访问 → 硬件产生 store/load page fault（`scause=15/13`）→ `usertrap` 先试 `vmfault`：`stval < p->sz` 成立，但 `ismapped()` 为真 → 返回 0（**不会把保护页补出来**）→ 落入致命分支 → 打印诊断 → `setkilled(p)` → `kexit(-1)`；
- 判定为"用户栈越界/非法访存、终止当前进程"而非内核崩溃：`kexit` 走 `sched()` 让出 CPU，父进程 `wait` 收到 `status=-1`，内核继续运行；
- 实测：`guardtest` 用 `sbrk(0)` 反推保护页地址并写入 → `usertrap(): unexpected scause 0xf … stval=0x1000` → `GUARDTEST PASS (status=-1)`。

### 5.4 受控拷贝策略

| 策略 | 开销 | 风险 |
| --- | --- | --- |
| 逐字节 | 每字节一次边界判断，最慢 | 最简单，无跨页漏判 |
| 按字对齐批量 | 快 | 首尾非对齐需特判，跨页仍要拆，易错 |
| **按页预校验 + 页内 `memmove`（选型）** | 每页一次 `walkaddr`（校验 `PTE_V/PTE_U`），页内一次 `memmove` | 单核、页表不会被并发改写，TOCTOU 不成立 |

跨页正确性实测（`pagecopy`）：`sbrk` 两页后构造 8 字节负载横跨页边界，`write(1, q, 8)` 输出 `payload=[ABCDEFGH]` 无损。

**grep 自查（直接解引用用户指针）**：用户指针入口只有 `console.c`（`either_copyin/out`）、`sysfile.c`（`read/write/exec` 名字）、`syscall.c`（`argstr → copyinstr`），全部走受控拷贝；`exec.c` 里的 `walkaddr` 只用于内核侧按物理地址写映像页。结论：**不存在直接解引用用户指针的路径**。

### 5.5 `sbrk`

- 用户库 `sbrk()` → `SYS_sbrk(n, SBRK_EAGER)`，`sbrklazy()` → `SBRK_LAZY`（调用号 12，随 lab2 增量包提供）；
- `n == 0` 返回当前 `p->sz`；`n > 0` 走 `growproc(n)`：`uvmalloc(pt, sz, sz+n, PTE_W)`（`PTE_W` 必须传，否则堆变只读），并拒绝 `sz+n > TRAPFRAME`；`n < 0` 走 `uvmdealloc`；
- **返回旧边界**（`badaccess` 的 `p[8192]`、`memcap` 的 `p[0..CHUNK)` 都依赖这一点）；
- 配额耗尽：`uvmalloc` 返回 0 → `sys_sbrk` 返回 −1，`p->sz` 不变，中间页由 `uvmalloc`/`mappages` 回滚，内核不 panic。实测 `memcap`：2029 轮 64KB 后 OOM 返回错误、已分配内存可读写、内核存活（`TEST-4 PASS`）；
- **LAZY 语义偏差**：`SBRK_LAZY` 分支当前实现的是 lab5 的延迟语义（只改 `p->sz`，缺页时由 `vmfault` 补页），与说明书"本实验统一按 EAGER 执行"存在偏差。官方两项测试都走 `sbrk()`（EAGER），不受影响；此偏差在此显式记录。

### 5.6 `dump_pagetable`

- 格式严格遵循 `docs/dump_pagetable-ABI.md`：`DUMP-PAGETABLE begin/end`，非叶打 `L2/L1 … perm=----`，叶子打 `LEAF va=… pa=… perm=rwxu`（R/W/X/U 四位、`-` 占位），按 va 升序，A/D 不输出；
- `va` 取该表项管辖区间起点（沿索引逐级 OR），`%p` 打印为固定 16 位十六进制，便于与图纸逐项比对；
- 调用点：`forkret` 中 `kexec("sh", 0)` 成功之后、`prepare_return()` 之前打印一次（首个用户进程装配完成）；
- 现场重放：`make dump` 跑一次 QEMU 并抓取 `DUMP-PAGETABLE begin/end` 之间的内容（`dump-callpoint-ABI.md` 第 3 条）。

实测输出（`sh` 的页表，与 5.1 布局逐项对应）：

```
DUMP-PAGETABLE begin
L2 va=0x0000000000000000 pa=0x0000000084063000 perm=----
L1 va=0x0000000000000000 pa=0x0000000083fb4000 perm=----
LEAF va=0x0000000000000000 pa=0x0000000083fb5000 perm=r-xu     ← 代码段+只读数据
LEAF va=0x0000000000001000 pa=0x0000000084064000 perm=rw-u     ← 数据段+.bss
LEAF va=0x0000000000002000 pa=0x0000000083fb3000 perm=rw--     ← guard 页
LEAF va=0x0000000000003000 pa=0x0000000084065000 perm=rw-u     ← 用户栈
L2 va=0x0000003fc0000000 pa=0x0000000083fb6000 perm=----
L1 va=0x0000003fffe00000 pa=0x0000000084062000 perm=----
LEAF va=0x0000003fffffe000 pa=0x000000008405f000 perm=rw--     ← TRAPFRAME
LEAF va=0x0000003ffffff000 pa=0x0000000080003000 perm=r-x-     ← TRAMPOLINE(与内核表同址)
DUMP-PAGETABLE end
```

其中 `perm=r-x-` 与 `perm=rw--` 两行的 `u` 位为空，正体现"跳板页与陷入帧页只给 S 态用"。

### 5.7 `struct proc` 的内存管理字段

`pagetable`（`allocproc` 建、`kexec` 提交时替换并回收旧的、`freeproc` 经 `proc_freepagetable` 销毁）、`sz`（进程内存大小 = 堆顶，`kexec`/`growproc` 维护，`copyin/copyout` 用它做上界）、`trapframe`（`allocproc` 分配、`proc_pagetable` 固定映射到 `TRAPFRAME`、`freeproc` 释放）、`kstack`（`procinit` 记下 `KSTACK(i)`，`kvmmake` 里映射）。

---

## 六、验证与回归

### 6.1 官方测试（`support/tests/`，已按 README 拷入 `user/` 并加入 `UPROGS`）

```
sh> badaccess
usertrap(): unexpected scause 0xf pid=3   sepc=0xc stval=0x0
TEST-3a PASS: wild write killed (status=-1)          ← 写代码页被杀(R+X 无 W)
usertrap(): unexpected scause 0xd pid=4   sepc=0x48 stval=0x6000
TEST-3b PASS: beyond-sbrk access killed (status=-1)  ← 越过 sbrk 顶被杀
sh> memcap
TEST-4 PASS: OOM returned error after 2029 chunks; existing alloc intact; kernel alive
```

### 6.2 说明书 §4 要求的两组自测用例（源码见附录 C）

| 用例 | 触发方式 | 期望 | 实测 |
| --- | --- | --- | --- |
| 保护页越界 | 子进程 `sbrk(0)` 反推保护页地址并写入 | 进程被安全终止、父进程 `wait` 非 0、`sh` 存活 | `scause 0xf stval=0x1000` → `GUARDTEST PASS (status=-1)` |
| 跨页受控拷贝 | `sbrk` 两页，8 字节负载横跨页边界后 `write` | 数据无损 | `PAGECOPY payload=[ABCDEFGH]` |

### 6.3 页表观测

`make dump` 输出见 5.6，与 5.1 的布局表逐项对齐；权限位逐条可解释。

### 6.4 功能回归

- `inject_uart.py`（lab2 压力测试）：`boot ok`、`EXPECT 失败 0 条`、退出码 0；
- lab1 banner 协议 2：`OSLAB1 sid=2024302111292 mod97=0x52` + `[chk=2290]`，与 `expect_banner.txt` 逐字节一致；
- `hi` / `spin` 正常；`sh` 与 `spin` 并发（`spin` 持续输出期间 `sh` 的 `wait` 正常回收）；
- 构建：`make` rc=0，仅保留既有的 `LOAD segment with RWX permissions` 链接警告（见 4.3）。

---

## 七、思考题要点

1. **TLB 未命中要访问几次物理内存？** SV39 三级页表，一次翻译最多 3 次页表访存（根/中间/叶子）＋ 1 次数据访问；现代处理器靠**页表缓存（PWC/页表遍历缓存）**、多级 TLB、以及大页（superpage）降低开销。
2. **拆除映射为何要先把 PTE 置无效、再释放物理页？** 反序会出现"页表项仍有效但物理页已空闲/已分配给他人"的窗口：此时任何仍能走到该 VA 的路径（TLB 尚未刷新、另一核、或并发线程）会把别人的数据当自己的读写——典型的 UAF/信息泄漏与数据破坏；本内核里 `kfree` 会把页填成垃圾，若 PTE 未清就会把 0x0101… 当作合法数据读出来。
3. **`copyin` 与 `memcpy` 的本质区别？** `memcpy` 假设源地址在本地址空间内合法，直接解引用；`copyin` 必须经页表校验（`PTE_V`/`PTE_U`/范围）后按物理地址搬运。若内核用 `memcpy` 处理用户指针，恶意程序可传入指向内核空间的地址，内核便以自己的权限代它读写内核数据（权限提升），或把内核数据当作用户缓冲写入而泄漏。

---

## 八、已知取舍与遗留

| 项 | 现状 | 影响 |
| --- | --- | --- |
| `uvmunmap` 不剪枝 | 空页表页留到 `freewalk` | 从 `uvmalloc` 层观测 `nfree` 不严格复原；对 `mappages` 层观测严格复原 |
| `user/user.ld` 改为本地版本（页对齐 + 段排布） | 说明书建议用派生链接脚本、保持预置文件不动 | 段边界不再依赖链接脚本符号，由 Makefile 从 ELF 段表计算并在构建期校验；验收时需说明该偏差 |
| guard 用哑映射而非空洞 | `uvmclear` 清 `PTE_U` | 与说明书"未映射空洞"的措辞不同但语义等价；`uvmcopy` 会照抄该页；`copyout` 目前只校验 `PTE_W`，内核理论上可写该页（未加 `PTE_U` 校验属刻意保留，见决策记录） |
| `SBRK_LAZY` 实现真延迟 | 只改 `p->sz`，缺页由 `vmfault` 补 | 与"lab3 统一 EAGER"有偏差，提前具备 lab5 语义 |
| 无 `heapbase` 下界 | `growproc(n<0)` 直接 `uvmdealloc` | 理论上可收缩进栈/guard 区（xv6 同款取舍） |
| `vmfault` 仍在 `copyin/copyout/copyinstr` 与 `usertrap` 链路上 | 保留为 lab5 的钩子 | EAGER 下无实际触发；若改成空洞式 guard，必须给它加范围限制 |
| 单核假设 | `sfence.vma` 只刷本核 | 多核需 TLB shootdown，作为思考题留档 |

---

## 附录 A：阶段一临时自检（源码 + 输出）

自检插在 `main()` 的 `kinit()` 之后、`kvminit()` 之前，验证完成后按说明书要求删除。

```c
static void kalloctest(void) {
  void* pg[8];
  uint64 usable = kalloc_usable();
  uint64 n0 = kalloc_nfree();
  int fail = 0;
  for (int i = 0; i < 8; i++) pg[i] = 0;
  printk("SELFTEST begin usable=%lu total=%lu delta=%d\n", usable, kalloc_total(), LAB3_MEMCAP_DELTA);
  for (int i = 0; i < 8; i++) {                     // 1) 对齐 + 2) 清零
    pg[i] = kalloc();
    if (pg[i] == 0) { printk("SELFTEST FAIL: kalloc returned 0 early\n"); fail = 1; break; }
    if (((uint64)pg[i] % PGSIZE) != 0) { printk("SELFTEST FAIL: not aligned\n"); fail = 1; }
    char* c = (char*)pg[i];
    for (int j = 0; j < PGSIZE; j++)
      if (c[j] != 0) { printk("SELFTEST FAIL: not zeroed\n"); fail = 1; break; }
  }
  printk("SELFTEST nfree after alloc8 = %lu (expect %lu)\n", kalloc_nfree(), n0 - 8);
  if (kalloc_nfree() != n0 - 8) fail = 1;
  for (int i = 0; i < 8; i++) if (pg[i]) { kfree(pg[i]); pg[i] = 0; }   // 3) 守恒
  printk("SELFTEST nfree after free8 = %lu (expect %lu)\n", kalloc_nfree(), n0);
  if (kalloc_nfree() != n0) fail = 1;
  for (int i = 0; i < 3; i++) { pg[i] = kalloc(); printk("SELFTEST order[%d] pa=%p\n", i, pg[i]); }
  for (int i = 0; i < 3; i++) { kfree(pg[i]); pg[i] = 0; }
  uint64 cnt = 0, maxpa = 0; void* p;               // 4) 耗尽 + 配额上界
  while ((p = kalloc()) != 0) { cnt++; if ((uint64)p > maxpa) maxpa = (uint64)p; }
  printk("SELFTEST exhaust cnt=%lu usable=%lu nfree=%lu maxpa=%p\n", cnt, usable, kalloc_nfree(), (void*)maxpa);
  if (cnt != usable || kalloc_nfree() != 0) fail = 1;
  if (maxpa >= PHYSTOP - (uint64)LAB3_MEMCAP_DELTA * PGSIZE) { printk("SELFTEST FAIL: reserved page handed out\n"); fail = 1; }
  kinit();                                          // 5) 重建位图复位
  printk("SELFTEST after reinit nfree=%lu (expect %lu)\n", kalloc_nfree(), usable);
  if (kalloc_nfree() != usable) fail = 1;
  printk("SELFTEST %s\n", fail ? "FAIL" : "PASS");
}
```

输出（详见 2.7）末行为 `SELFTEST PASS`。

## 附录 B：阶段二临时自检（源码要点 + 输出）

自检插在 `main()` 的 `kvminithart()` 之后，验证完成后删除。要点（完整实现见交付前的临时版本）：

```c
  // A. 成功路径: 3 页映射 + walk 反查 pa 连续性与 perm, 再 uvmunmap(do_free=0)+uvmfree
  // B. 失败回滚: uvmcreate 一张空表 -> 用"页内链式暂存"把空闲页消耗到只剩 3 页
  //    -> mappages(pt, big, 2048*PGSIZE, fake, PTE_R|PTE_W) 必然中途失败
  //    -> 断言 rc==-1 && kalloc_nfree()==3 && 区间内无有效 PTE && walk(pt,big,0)==0
  //    -> 释放链与页表, 断言 nfree 回到测试前
  // C. 重复映射: 期望 panic("mappages: remap")(放最后)
  void* chain = 0; uint64 cnt = 0;                 // 页内链式暂存, 避免几十万字节的指针数组
  while (kalloc_nfree() > 3) { void* p = kalloc(); if (!p) break; *(void**)p = chain; chain = p; cnt++; }
```

输出见 3.8（`SELFTEST2 PASS` + `panic: mappages: remap`）。

## 附录 C：阶段四两组自测用例（源码 + 输出）

用例 1 `user/guardtest.c`（临时程序，验证后从树中删除）：

```c
#include "kernel/types.h"
#include "kernel/course_sid.h"
#include "user/user.h"
int main(void){
  int pid = fork();
  if(pid == 0){
    char *brk = sbrk(0);                                  // 堆顶 = 映像末尾 + guard + 栈
    char *guard = brk - (1 + LAB3_GUARD_PAGES) * 4096;    // 栈下方第一页 = 保护页
    *(volatile char *)guard = 0x5a;                       // 必须被杀
    printf("GUARDTEST FAIL: guard write survived\n");
    exit(0);
  }
  int st; wait(&st);
  printf("GUARDTEST %s (status=%d)\n", st != 0 ? "PASS" : "FAIL", st);
  exit(st != 0 ? 0 : 1);
}
```

用例 2 `user/pagecopy.c`（临时程序，验证后从树中删除）：

```c
#include "kernel/types.h"
#include "user/user.h"
int main(void){
  char *p = sbrk(2 * 4096);
  if(p == (char *)-1){ printf("PAGECOPY FAIL: sbrk\n"); exit(1); }
  char *q = p + 4096 - 4;                                 // 8 字节负载横跨页边界
  q[0]='A'; q[1]='B'; q[2]='C'; q[3]='D'; q[4]='E'; q[5]='F'; q[6]='G'; q[7]='H';
  printf("PAGECOPY payload=[");
  write(1, q, 8);                                         // 跨页 write: copyin 逐页拼接
  printf("]\nPAGECOPY DONE\n");
  exit(0);
}
```

输出：

```
sh> guardtest
usertrap(): unexpected scause 0xf pid=6
            sepc=0x16 stval=0x1000
GUARDTEST PASS (status=-1)
sh> pagecopy
PAGECOPY payload=[ABCDEFGH]
PAGECOPY DONE
```

## 附录 D：直接解引用用户指针的自查

```
$ grep -rn "argaddr\|argint\|argstr" kernel/*.c | grep -v syscall.c
kernel/sysfile.c:37,56   argint/argaddr → consolewrite/consoleread(1, …) → either_copyin/out → copyin/copyout
kernel/sysfile.c:75      argstr      → copyinstr
kernel/sysproc.c:8,23    argint/argaddr → 只传值, 不解引用
```

结论：所有来自用户空间的地址都经 `copyin/copyout/copyinstr`（内部 `walkaddr` 校验 `PTE_V/PTE_U`）搬运，无直接解引用；`exec.c` 中的 `walkaddr` 仅用于内核侧把映像字节写入已映射的用户页。
