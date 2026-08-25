// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;
#define NPAGES ((PHYSTOP - KERNBASE) / PGSIZE)

struct {
  struct spinlock lock;
  int count[NPAGES];
} kref;

static int
refindex(uint64 pa)
{
  return (pa - KERNBASE) / PGSIZE;
}
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&kref.lock, "kref");

  // 初始设为1；freerange中的kfree会将可用页减为0。
  for(int i = 0; i < NPAGES; i++)
    kref.count[i] = 1;

  freerange(end, (void*)PHYSTOP);
}
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int idx;

  if(((uint64)pa % PGSIZE) != 0 ||
     (char*)pa < end ||
     (uint64)pa >= PHYSTOP)
    panic("kfree");

  idx = refindex((uint64)pa);

  acquire(&kref.lock);

  if(kref.count[idx] <= 0)
    panic("kfree ref");

  kref.count[idx]--;

  if(kref.count[idx] > 0){
    release(&kref.lock);
    return;
  }

  release(&kref.lock);

  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}
// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
if(r){
  acquire(&kref.lock);
  kref.count[refindex((uint64)r)] = 1;
  release(&kref.lock);

  memset((char*)r, 5, PGSIZE);
}
  return (void*)r;
}
void
krefinc(void *pa)
{
  uint64 addr = (uint64)pa;

  if((addr % PGSIZE) != 0 ||
     addr < KERNBASE ||
     addr >= PHYSTOP)
    panic("krefinc");

  acquire(&kref.lock);

  if(kref.count[refindex(addr)] <= 0)
    panic("krefinc ref");

  kref.count[refindex(addr)]++;

  release(&kref.lock);
}

int
krefcnt(void *pa)
{
  uint64 addr = (uint64)pa;
  int count;

  if((addr % PGSIZE) != 0 ||
     addr < KERNBASE ||
     addr >= PHYSTOP)
    panic("krefcnt");

  acquire(&kref.lock);
  count = kref.count[refindex(addr)];
  release(&kref.lock);

  return count;
}
