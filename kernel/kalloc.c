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
} kmem[NCPU];

void
kinit()
{
  for(int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem");

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

void
kfree(void *pa)
{
  struct run *r;
  int id;

  if(((uint64)pa % PGSIZE) != 0 ||
     (char*)pa < end ||
     (uint64)pa >= PHYSTOP)
    panic("kfree");

  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;

  // cpuid()只能在中断关闭时使用。
  push_off();
  id = cpuid();

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);

  pop_off();
}

void *
kalloc(void)
{
  struct run *r = 0;
  int id;

  push_off();
  id = cpuid();

  // 优先从当前CPU分配。
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);

  if(r == 0){
    // 优先从其他CPU偷取大批页面，但给对方保留64页。
    for(int offset = 1; offset < NCPU; offset++){
      int victim = (id + offset) % NCPU;
      struct run *batch = 0;

      acquire(&kmem[victim].lock);

      struct run *keep = kmem[victim].freelist;
      if(keep){
        int nkeep = 1;

        while(nkeep < 64 && keep->next){
          keep = keep->next;
          nkeep++;
        }

        if(nkeep == 64 && keep->next){
          batch = keep->next;
          keep->next = 0;
        }
      }

      release(&kmem[victim].lock);

      if(batch){
        r = batch;
        struct run *rest = r->next;
        r->next = 0;

        if(rest){
          acquire(&kmem[id].lock);
          kmem[id].freelist = rest;
          release(&kmem[id].lock);
        }

        break;
      }
    }
  }

  // 内存很少时，允许从其他CPU偷取单独一页，避免虚假OOM。
  if(r == 0){
    for(int offset = 1; offset < NCPU; offset++){
      int victim = (id + offset) % NCPU;

      acquire(&kmem[victim].lock);

      r = kmem[victim].freelist;
      if(r)
        kmem[victim].freelist = r->next;

      release(&kmem[victim].lock);

      if(r){
        r->next = 0;
        break;
      }
    }
  }

  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE);

  return (void*)r;
}
