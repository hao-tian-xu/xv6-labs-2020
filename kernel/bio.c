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

#define NBUCKET 13

struct {
  struct spinlock lock;
  struct spinlock hashlock[NBUCKET];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf hashtable[NBUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for (int i = 0; i < NBUCKET; i++)
    initlock(&bcache.hashlock[i], "bcache");

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b, *temp;
  int hash = blockno % NBUCKET;

  acquire(&bcache.lock);
  acquire(&bcache.hashlock[hash]);

  // Is the block already cached?
  for (b = bcache.hashtable[hash].next; b != 0; b = b->next) {
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.hashlock[hash]);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  int ihash;
  for (ihash = 0; ihash < NBUCKET; ihash++) {
    if (ihash != hash) acquire(&bcache.hashlock[ihash]);
    for (temp = bcache.buf + ihash; temp < bcache.buf + NBUF; temp += NBUCKET) {
      if (temp->refcnt == 0) {
        b = temp;
        break;
      }
    }
    if (b) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;

      // memo: add b to hashtable
      b->next = bcache.hashtable[hash].next;
      bcache.hashtable[hash].next = b;
      if (ihash != hash) release(&bcache.hashlock[ihash]);
      release(&bcache.hashlock[hash]);

      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
    if (ihash != hash) release(&bcache.hashlock[ihash]);
  }
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

  int hash = b->blockno % NBUCKET;
  struct buf *temp;

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  acquire(&bcache.hashlock[hash]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    for (temp = &bcache.hashtable[hash]; temp != 0; temp = temp->next) {
      if (temp->next == b) {
        temp->next = b->next;
        b->next = 0;
        break;
      }
    }
  }

  release(&bcache.hashlock[hash]);
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


