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

#ifdef LAB_LOCK_MINE
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];
#else
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;
#endif

void
kinit()
{
#ifdef LAB_LOCK_MINE
  char* kmem_name = "kmem0";
  for(int i = 0; i < NCPU; i++){
    snprintf(kmem_name, 4096, "kmem%d", i);
    initlock(&kmem[i].lock, kmem_name);
  }
#else
  initlock(&kmem.lock, "kmem");
#endif
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

// Free the page of physical memory pointed at by v,
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

#ifdef LAB_LOCK_MINE
  push_off();
  int i = cpuid();
  pop_off();

  acquire(&kmem[i].lock);
  r->next = kmem[i].freelist;
  kmem[i].freelist = r;
  release(&kmem[i].lock);
#else
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
#endif
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

#ifdef LAB_LOCK_MINE
  push_off();
  int i = cpuid();
  pop_off();

  acquire(&kmem[i].lock);
  r = kmem[i].freelist;
  if(r) {
    kmem[i].freelist = r->next;
    release(&kmem[i].lock);
  } else{
    release(&kmem[i].lock);
    for(int j = 0; j < NCPU; j++) {
      if(j == i) continue;
      acquire(&kmem[j].lock);
      r = kmem[j].freelist;
      if(r) {
        kmem[j].freelist = r->next;
        release(&kmem[j].lock);
        break;
      }
      release(&kmem[j].lock);
    }
  }
#else
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
#endif

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
