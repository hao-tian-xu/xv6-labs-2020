// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#ifdef LAB_LOCK_2
#define NBUCKET 13

#ifdef LAB_LOCK_ARCHIVE
struct entry {
  uint blockno;
  struct buf* buf;
  struct entry* next;
};
struct entry* table[NBUCKET];
#endif

struct buf table[NBUCKET];

struct {
  struct spinlock lock;

  struct spinlock bucketlock[NBUCKET];
  struct buf buf[NBUF];
} bcache;
#else
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;
#endif

void
binit(void)
{
  struct buf *b;
#ifdef LAB_LOCK_2
  int i;

  initlock(&bcache.lock, "bcache");
  char* bcache_bucket_name = "bcache_bucket00";
  for(i = 0; i < NBUCKET; i++){
    snprintf(bcache_bucket_name, 4096, "bcache_bucket%d", i);
    initlock(&bcache.bucketlock[i], "bcache");
    table[i].next = &table[i];
    table[i].prev = &table[i];
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++) {
    initsleeplock(&b->lock, "buffer");
//    b->refcnt = 0;
//    b->timestamp = 0;
  }
#else

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
#endif
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno) {
  struct buf *b;

#ifdef LAB_LOCK_2
  struct buf* lru = 0;
  uint bucketno = blockno % NBUCKET;

  acquire(&bcache.bucketlock[bucketno]);
  for (b = table[bucketno].next; b != &table[bucketno]; b = b->next) {
    if (b->blockno == blockno && b->dev == dev) {
      b->refcnt++;
      b->timestamp = ticks;
      release(&bcache.bucketlock[bucketno]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // memo: avoid deadlock
  release(&bcache.bucketlock[bucketno]);

  acquire(&bcache.lock);

  uint timestamp = ticks;
  for (int i = 0; i < NBUF; i++) {
    b = &bcache.buf[i];
    if (b->refcnt == 0 && b->timestamp <= timestamp) {
      timestamp = b->timestamp;
      lru = b;
    }
  }
  if (lru != 0) {
    uint old_bucketno = lru->blockno % NBUCKET;

    acquire(&bcache.bucketlock[bucketno]);
    if (old_bucketno != bucketno)
      acquire(&bcache.bucketlock[old_bucketno]);

    lru->dev = dev;
    lru->blockno = blockno;
    lru->valid = 0;
    lru->refcnt = 1;
    lru->timestamp = ticks;

    // update hashtable
    if (table[bucketno].next == &table[bucketno]) {
      lru->prev = &table[bucketno];
      lru->next = &table[bucketno];
      table[bucketno].next = lru;
      table[bucketno].prev = lru;
    }
    else {
      lru->next = table[bucketno].next;
      lru->prev = &table[bucketno];
      table[bucketno].next->prev = lru;
      table[bucketno].next = lru;
    }

    if (old_bucketno != bucketno)
      release(&bcache.bucketlock[old_bucketno]);
    release(&bcache.bucketlock[bucketno]);

    release(&bcache.lock);
    acquiresleep(&lru->lock);
    return lru;
  }
  release(&bcache.lock);

#ifdef LAB_LOCK_ARCHIVE
  for(i = 0; i < NBUCKET; i++) {
    acquire(&bcache.bucketlock[i]);
    for(j = i; j < NBUCKET; j += NBUCKET) {
      *b = bcache.buf[j];
      if(b->dev == dev && b->blockno == blockno) {
        b->refcnt++;
        b->timestamp = ticks;
        b->bucketno = i;
        release(&bcache.bucketlock[i]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.bucketlock[i]);
  }

  for(i = 0; i < NBUCKET; i++) {
    acquire(&bcache.bucketlock[i]);
    for(j = i; j < NBUCKET; j += NBUCKET) {
      *b = bcache.buf[j];
      if(b->refcnt == 0) {
        if(b->timestamp <= timestamp) {
          timestamp = b->timestamp;
          lru = b;
          lru_i = i;
        }
      }
    }
    release(&bcache.bucketlock[i]);
  }
  if(lru) {
    acquire(&bcache.bucketlock[lru_i]);
    lru->dev = dev;
    lru->blockno = blockno;
    lru->valid = 0;
    lru->refcnt = 1;
    lru->timestamp = ticks;
    b->bucketno = lru_i;
    release(&bcache.bucketlock[lru_i]);
    acquiresleep(&lru->lock);
    return lru;
  }
#endif
#else
  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
#endif
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

#ifdef LAB_LOCK_2
  uint bucketno = b->blockno % NBUCKET;

  acquire(&bcache.bucketlock[bucketno]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->prev->next = b->next;
    b->next->prev = b->prev;
  }
  release(&bcache.bucketlock[bucketno]);
#ifdef  LAB_LOCK_ARCHIVE
    if (parent->blockno == b->blockno)
      table[bucketno] = parent->next;
    else {
      for (e = table[bucketno]->next; e != 0; e = e->next) {
        if (e->blockno == b->blockno) {
          e->buf = 0;
          parent->next = e->next;
          break;
        }
        parent = e;
      }
    }
#endif
#else
  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
#endif
}

void
bpin(struct buf *b) {
#ifdef LAB_LOCK_2
  uint bucketno = b->blockno % NBUCKET;
  acquire(&bcache.bucketlock[bucketno]);
  b->refcnt++;
  release(&bcache.bucketlock[bucketno]);
#else
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
#endif
}

void
bunpin(struct buf *b) {
#ifdef LAB_LOCK_2
  uint bucketno = b->blockno % NBUCKET;
  acquire(&bcache.bucketlock[bucketno]);
  b->refcnt--;
  release(&bcache.bucketlock[bucketno]);
#else
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
#endif
}


