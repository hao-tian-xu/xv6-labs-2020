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
  struct spinlock lock;   // memo: a single lock for physical memory allocation and free
  struct run *freelist;
} kmem;

#ifdef LAB_COW
int refcount[(PHYSTOP - KERNBASE) / PGSIZE];
#define refcount_init(pa) (refcount[((uint64)pa - KERNBASE) / PGSIZE] = 1)
#define refcount_decrease(pa) (refcount[((uint64)pa - KERNBASE) / PGSIZE]--)
#define refcount_get(pa) (refcount[((uint64)pa - KERNBASE) / PGSIZE])

void
refcount_increase(uint64 pa)
{
  refcount[(pa - KERNBASE) / PGSIZE]++;
}
#endif

void
kinit()
{
#ifdef LAB_COW
  for (int i = 0; i < ((PHYSTOP - KERNBASE) / PGSIZE); i++)
    refcount[i] = 0;
#endif
  initlock(&kmem.lock, "kmem");
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

#ifdef LAB_COW
  refcount_decrease(pa);
  if (refcount_get(pa) > 0)
    return;
#endif

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

#ifdef LAB_COW
  if(r)
    refcount_init(r);
#endif

  return (void*)r;
}
