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
//
// The implementation uses two state flags internally:
// * B_VALID: the buffer data has been read from the disk.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  int isfull; //beg 함수의 재귀적호출을 막기 위한 flag

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

//PAGEBREAK!
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
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  //버퍼가 가득찼다면 sync호출 beget의 재귀적 호출을 막기 위해 isfull flag를 둠
  if(bcache.isfull == 0 && buffer_isfull())
  {
    acquire(&bcache.lock);
    bcache.isfull = 1;
    release(&bcache.lock);

    sync();

    acquire(&bcache.lock);
    bcache.isfull = 0;
    release(&bcache.lock);
  }

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

  // Not cached; recycle an unused buffer.
  // Even if refcnt==0, B_DIRTY indicates a buffer is in use
  // because log.c has modified it but not yet committed it.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->flags = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if((b->flags & B_VALID) == 0) {
    iderw(b);
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  b->flags |= B_DIRTY;
  iderw(b);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

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
}
//PAGEBREAK!
// Blank page.

// dirty버퍼들을 찾아 로그에 기록하고 개수 반환
int
logDirtyBuffer(void)
{
  struct buf *b;

  int cnt = 0;
  acquire(&bcache.lock); 
  // 버퍼가 dirty 상태면 로그에 기록
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if((b->flags & B_DIRTY)){
      log_write(b);
      cnt++;
    }
  }
  release(&bcache.lock); 

  return cnt; //dirtybuffer 개수 반환
}

//버퍼 캐시가 가득 찼는지 확인
int
buffer_isfull(void)
{
  struct buf* b = 0;
  int cnt = 0;
  acquire(&bcache.lock); 
  //버퍼가 dirty 상태면 cnt증가
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if((b->flags & B_DIRTY)){
      cnt++;
    }
  }
  release(&bcache.lock);
  if(cnt >= NBUF-3) return 1; //dirty 버퍼가 가득차있으면 1반환
  else return 0;
}