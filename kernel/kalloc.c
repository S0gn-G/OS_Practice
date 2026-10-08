//
// 物理页分配器(lab3 阶段一):位图空闲管理 + 中点顺序 + 内存配额。
//
//   位图: 有序空闲链表单页释放是 O(n), memcap 的万页级回收会退化成 O(n^2);
//         位图 1 bit/页(128MB 仅 4KB 元数据, 静态放 .bss), 释放 O(1),
//         分配顺序只是"扫描方向"的选择。
//   中点: 本学号 LAB3_ALLOC_ORDER=2, 策略直接写死为"从受管区间中点向两侧
//         扩散", 不做宏分支。
//   配额: 内存上限 = 总页数 - LAB3_MEMCAP_DELTA。kinit 只把前 usable_pages 页
//         标为空闲, 顶部 delta 页从不进位图 —— 上限是硬保证, nfree 归零即 OOM。
//
#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "course_sid.h"
#include "defs.h"

extern char end[]; // first address after kernel, defined by kernel.ld

// 位图按"全部 RAM 都受管"的上界静态开辟: (PHYSTOP-KERNBASE)/PGSIZE/64 = 512 字
#define BITMAP_WORDS (((PHYSTOP - KERNBASE) / PGSIZE) / 64)

static uint64 freebitmap[BITMAP_WORDS];

static uint64 managed_start; // 第一个受管物理页(已页对齐)
static uint64 total_pages;   // 受管页数
static uint64 usable_pages;  // 可分配页数 = total_pages - LAB3_MEMCAP_DELTA
static uint64 nfree;         // 当前空闲(可分配)页数

static uint64 mid;              // 中点序起点
static uint64 cur_up, cur_down; // 两个只前进的游标
static int up_alive, down_alive;

#define BIT_ISSET(i) (((freebitmap[(i) / 64]) >> ((i) % 64)) & 1UL)
#define BIT_SET(i) (freebitmap[(i) / 64] |= (1UL << ((i) % 64)))
#define BIT_CLR(i) (freebitmap[(i) / 64] &= ~(1UL << ((i) % 64)))

//
// 物理地址 -> 位图下标。只接受受管且可分配区间内的页: 顶部 LAB3_MEMCAP_DELTA
// 页是保留配额, 从未发出, 因此合法释放不会落到那里。
//
static int pa2idx(uint64 pa, uint64* idx) {
  if ((pa % PGSIZE) != 0 || pa < managed_start)
    return -1;
  uint64 i = (pa - managed_start) / PGSIZE;
  if (i >= usable_pages)
    return -1;
  *idx = i;
  return 0;
}

//
// 从 from(含)向上找第一个空闲位; 成功写 *out 返回 0, 找不到返回 -1。
// 逐位判而不用 __builtin_ctzl: rv64gc 无 Zbb, 内建函数会落到 libgcc 的
// __ctzdi2, 而本工程用裸 ld + -nostdlib 链接, 会造成未定义符号。
//
static int find_up(uint64 from, uint64* out) {
  if (from >= usable_pages)
    return -1;
  uint64 w = from / 64;
  uint64 b = from % 64;
  for (;;) {
    uint64 word = freebitmap[w];
    for (; b < 64; b++) {
      if (word & (1UL << b)) {
        uint64 i = w * 64 + b;
        if (i >= usable_pages)
          return -1; // 哨兵位, 正常不会置位
        *out = i;
        return 0;
      }
    }
    if (++w >= (uint64)BITMAP_WORDS)
      return -1;
    b = 0;
  }
}

// 从 from(含)向下找第一个空闲位。
static int find_down(uint64 from, uint64* out) {
  if (from >= usable_pages)
    from = usable_pages - 1;
  uint64 w = from / 64;
  uint64 b = from % 64;
  for (;;) {
    uint64 word = freebitmap[w];
    for (;;) {
      if (word & (1UL << b)) {
        *out = w * 64 + b;
        return 0;
      }
      if (b == 0)
        break;
      b--;
    }
    if (w == 0)
      return -1;
    w--;
    b = 63;
  }
}

//
// 中点序: 分配序列为 mid, mid-1, mid+1, mid-2, mid+2, ...
// 每轮先试离 mid 更近的一侧(等距时优先低地址, 保证首取正是 mid); 某方向扫到
// 边界即报废; 两游标都报废而 nfree 仍大于 0, 说明释放出的空洞落在游标身后,
// 此时把游标重置回 mid 再试一轮(回绕)。
//
static int mid_alloc(uint64* out) {
  int wrapped = 0;

  for (;;) {
    uint64 du = up_alive ? (cur_up - mid) : (uint64)-1;
    uint64 dd = down_alive ? (mid - cur_down) : (uint64)-1;

    if (!up_alive && !down_alive) {
      if (wrapped)
        return -1; // 真的没有空闲页了
      cur_up = mid;
      cur_down = (mid == 0) ? 0 : mid - 1;
      up_alive = 1;
      down_alive = (mid > 0);
      wrapped = 1;
      continue;
    }

    if (dd <= du) {
      if (find_down(cur_down, out) == 0) {
        if (*out == 0)
          down_alive = 0;
        else
          cur_down = *out - 1;
        return 0;
      }
      down_alive = 0; // 本侧已扫尽
    } else {
      if (find_up(cur_up, out) == 0) {
        if (*out + 1 >= usable_pages)
          up_alive = 0;
        else
          cur_up = *out + 1;
        return 0;
      }
      up_alive = 0;
    }
  }
}

//
// 初始化: 界定受管区间, 把前 usable_pages 页标为空闲(顶部 delta 页不置位)。
//
void kinit() {
  managed_start = PGROUNDUP((uint64)end);
  if (managed_start >= PHYSTOP)
    panic("kinit: no managed memory");
  total_pages = (PHYSTOP - managed_start) / PGSIZE;
  if (total_pages <= (uint64)LAB3_MEMCAP_DELTA)
    panic("kinit: LAB3_MEMCAP_DELTA too large");
  usable_pages = total_pages - (uint64)LAB3_MEMCAP_DELTA;

  memset(freebitmap, 0, sizeof(freebitmap));
  for (uint64 i = 0; i < usable_pages; i++)
    BIT_SET(i);
  nfree = usable_pages;

  mid = usable_pages / 2;
  cur_up = mid;
  cur_down = (mid == 0) ? 0 : mid - 1;
  up_alive = 1;
  down_alive = (mid > 0);
}

//
// 释放一页。非法地址/未对齐/重复释放都视为不变量违反, 直接 panic;
// 释放后填垃圾数据, 便于暴露悬挂引用(use-after-free)。
//
void kfree(void* pa) {
  uint64 idx;

  if (pa2idx((uint64)pa, &idx) != 0)
    panic("kfree: bad pa");
  if (BIT_ISSET(idx))
    panic("kfree: double free");

  memset(pa, 0x01, PGSIZE); // junk

  BIT_SET(idx);
  nfree++;
}

//
// 分配一页: 4KB 对齐、内容清零(数据安全隔离); 无空闲页(含触顶配额)返回空指针。
//
void* kalloc(void) {
  uint64 idx;

  if (nfree == 0)
    return 0;
  if (mid_alloc(&idx) != 0)
    panic("kalloc: nfree inconsistent"); // nfree>0 却找不到空闲位 = 位图坏了

  BIT_CLR(idx);
  nfree--;

  void* pa = (void*)(managed_start + idx * PGSIZE);
  memset(pa, 0, PGSIZE);
  return pa;
}

// ---- 只读统计接口(自检、回滚验证与现场演示用) ----

uint64 kalloc_nfree(void) {
  return nfree;
}

uint64 kalloc_total(void) {
  return total_pages;
}

uint64 kalloc_usable(void) {
  return usable_pages;
}
