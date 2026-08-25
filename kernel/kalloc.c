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

struct {
  struct spinlock lock;
  struct run *freelist;
} supermem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&supermem.lock, "supermem");
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

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
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

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void *
superalloc(void)
{
  char *pa;
  char *start = (char*)SUPERPGROUNDUP((uint64)end);

  for(pa = start; pa + SUPERPGSIZE <= (char*)PHYSTOP; pa += SUPERPGSIZE){
    int ok = 1;

    // 先检查这一整块 2MB 的每个 4KB 页是否都在 freelist 里
    for(char *p = pa; p < pa + SUPERPGSIZE; p += PGSIZE){
      int found = 0;

      acquire(&kmem.lock);
      for(struct run *r = kmem.freelist; r; r = r->next){
        if((char*)r == p){
          found = 1;
          break;
        }
      }
      release(&kmem.lock);

      if(!found){
        ok = 0;
        break;
      }
    }

    if(!ok)
      continue;

    // 再把这 512 个 4KB 页从 freelist 里摘掉
    for(char *p = pa; p < pa + SUPERPGSIZE; p += PGSIZE){
      acquire(&kmem.lock);
      struct run **rr = &kmem.freelist;
      while(*rr){
        if((char*)(*rr) == p){
          *rr = (*rr)->next;
          break;
        }
        rr = &(*rr)->next;
      }
      release(&kmem.lock);
    }

    memset(pa, 5, SUPERPGSIZE);
    return pa;
  }

  return 0;
}

void
superfree(void *pa)
{
  if(((uint64)pa % SUPERPGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("superfree");

  for(char *p = (char*)pa; p < (char*)pa + SUPERPGSIZE; p += PGSIZE)
    kfree(p);
}
