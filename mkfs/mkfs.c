#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat  // avoid clash with host struct stat
#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "kernel/param.h"

#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

#ifndef SNU
// NINODES moved to kernel/param.h
#define NINODES 200
#endif

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]
// if SNU
// [ boot block | sb block | log | FAT blocks | inode blocks | data blocks ]

int nbitmap = FSSIZE/(BSIZE*8) + 1;
int ninodeblocks = NINODES / IPB + 1;
int nlog = LOGSIZE;
int nmeta;    // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks;  // Number of data blocks
#ifdef SNU
int nFAT = (FSSIZE * 4) / BSIZE + 1;
#endif

int fsfd;
struct superblock sb;
char zeroes[BSIZE];
uint freeinode = 1;
uint freeblock;


void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);
void die(const char *);

// convert to riscv byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;


  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 2){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0)
    die(argv[1]);

  // 1 fs block = 1 disk sector
  #ifdef SNU
  nmeta = 2 + nlog + nFAT + ninodeblocks;
  #else
  nmeta = 2 + nlog + ninodeblocks + nbitmap;
  #endif
  nblocks = FSSIZE - nmeta;

  #ifdef SNU
  sb.magic = FSMAGIC_FATTY;
  #else
  sb.magic = FSMAGIC;
  #endif
  sb.size = xint(FSSIZE);
  sb.nblocks = xint(nblocks);
  sb.ninodes = xint(NINODES);
  sb.nlog = xint(nlog);
  sb.logstart = xint(2);
  #ifdef SNU
  sb.nfat = xint(nFAT);
  sb.fatstart = xint(2+nlog);
  sb.inodestart = xint(2+nlog+nFAT);
  sb.freehead = xint(2+nlog+nFAT+ninodeblocks);
  sb.freeblks = xint(nblocks);

  printf("nmeta %d (boot, super, log blocks %u fat blocks %u, inode blocks %u) blocks %d total %d\n",
         nmeta, nlog, nFAT, ninodeblocks, nblocks, FSSIZE);

  #else
  sb.inodestart = xint(2+nlog);
  sb.bmapstart = xint(2+nlog+ninodeblocks);

  printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);
  #endif

  freeblock = nmeta;     // the first free block that we can allocate

  for(i = 0; i < FSSIZE; i++)
    wsect(i, zeroes);

  #ifdef SNU
  balloc(freeblock);
  #else
  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);
  #endif

  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  for(i = 2; i < argc; i++){
    // get rid of "user/"
    char *shortname;
    if(strncmp(argv[i], "user/", 5) == 0)
      shortname = argv[i] + 5;
    else
      shortname = argv[i];

    assert(index(shortname, '/') == 0);

    if((fd = open(argv[i], 0)) < 0)
      die(argv[i]);

    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    if(shortname[0] == '_')
      shortname += 1;

    inum = ialloc(T_FILE);

    bzero(&de, sizeof(de));
    de.inum = xshort(inum);
    strncpy(de.name, shortname, DIRSIZ);
    iappend(rootino, &de, sizeof(de));

    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    close(fd);
  }

  // fix size of root inode dir
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off/BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(rootino, &din);

  #ifdef SNU
  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);
  #else
  balloc(freeblock);
  #endif

  exit(0);
}

void
wsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  if(write(fsfd, buf, BSIZE) != BSIZE)
    die("write");
}

void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  if(read(fsfd, buf, BSIZE) != BSIZE)
    die("read");
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

void
balloc(int used)
{
  #ifdef SNU
  uint buf[BSIZE * nFAT / sizeof(uint)];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  bzero(buf, BSIZE * nFAT);
  for(i = 0; i < used; i++){
    buf[i] = xint(-1);
  }
  for(i = used; i < FSSIZE; i++){
    buf[i] = xint(i+1);
  }
  printf("balloc: write %d fat blocks at sector %d\n", sb.nfat, sb.fatstart);
  for(i = 0; i < nFAT; i++){
    wsect(sb.fatstart + i, buf + (i*BSIZE/sizeof(uint)));
  }
  #else
  uchar buf[BSIZE];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < BSIZE*8);
  bzero(buf, BSIZE);
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);
  #endif
}

#define min(a, b) ((a) < (b) ? (a) : (b))

#ifdef SNU
uint inum2fat(uint inum)
{
  uint buf[BSIZE / sizeof(uint)];
  uint bn = inum / (BSIZE / sizeof(uint));
  uint off = inum % (BSIZE / sizeof(uint));
  uint blk = sb.fatstart + bn;

  rsect(blk, buf);
  uint fat = buf[off];
  // printf("requested inum: %u, fat: %u\n", inum, fat);
  return fat;
}

uint getfreeblk(uint prevblk){
  uint buf[BSIZE / sizeof(uint)];
  uint bn = sb.freehead / (BSIZE / sizeof(uint));
  uint off = sb.freehead % (BSIZE / sizeof(uint));
  uint blk = sb.fatstart + bn;


  // printf("bn: %u, off: %u, blk: %u, inum: %x\n", bn, off, blk, sb.freehead);
  rsect(blk, buf);
  // printf("buf:");
  // for (int i = 0; i < BSIZE/4; i++)
  //   printf("%x ", buf[i]);
  // printf("\n");
  uint freeblk = sb.freehead;
  sb.freehead = buf[off];
  buf[off] = 0;

  if (prevblk)
  {
    uint pbn = prevblk / (BSIZE / sizeof(uint));
    uint poff = prevblk % (BSIZE / sizeof(uint));
    uint pblk = sb.fatstart + pbn;
    if (blk == pblk){
      buf[poff] = freeblk;
      wsect(blk, buf);
    }
    else {
      wsect(blk, buf);
      rsect(pblk, buf);
      buf[poff] = freeblk;
      wsect(pblk, buf);
    }
  }
  else
    wsect(blk, buf);
  --sb.freeblks;
  return freeblk;
}
#endif

void
iappend(uint inum, void *xp, int n)
{
  // printf("iappend inum:%d, xp:%p, n:%d\n", inum, xp, n);
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint x;
  uint fat;

  rinode(inum, &din);
  off = xint(din.size);

  #ifdef SNU
  while(n > 0){
    if(xint(din.startblk) == 0){
      din.startblk = getfreeblk(0);
    }
    fbn = off / BSIZE;
    x = xint(din.startblk);
    for (int i = 0; i < fbn; i++)
    {
      if ((fat = inum2fat(x)) == 0)
        x = getfreeblk(x);
      else
        x = fat;
    }

    n1 = min(n, BSIZE - (off % BSIZE));
    rsect(x, buf);
    bcopy(p, buf + (off % BSIZE), n1);
    wsect(x, buf);
    // printf("writed %d characters on %p\n", n1, buf + (off % BSIZE));
    n -= n1;
    off += n1;
    // printf("remaining n: %u\n", n);
  }
  din.size = xint(off);
  winode(inum, &din);
  #else
  uint indirect[NINDIRECT];

  // printf("append inum %d at off %d sz %d\n", inum, off, n);
  while(n > 0){
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT){
      if(xint(din.addrs[fbn]) == 0){
        din.addrs[fbn] = xint(freeblock++);
      }
      x = xint(din.addrs[fbn]);
    } else {
      if(xint(din.addrs[NDIRECT]) == 0){
        din.addrs[NDIRECT] = xint(freeblock++);
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      if(indirect[fbn - NDIRECT] == 0){
        indirect[fbn - NDIRECT] = xint(freeblock++);
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      }
      x = xint(indirect[fbn-NDIRECT]);
    }
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
  #endif
}

void
die(const char *s)
{
  perror(s);
  exit(1);
}
