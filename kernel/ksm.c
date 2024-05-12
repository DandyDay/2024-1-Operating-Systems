//----------------------------------------------------------------
//
//  4190.307 Operating Systems (Spring 2024)
//
//  Project #4: KSM (Kernel Samepage Merging)
//
//  May 7, 2024
//
//  Dept. of Computer Science and Engineering
//  Seoul National University
//
//----------------------------------------------------------------

#include "param.h"
#include "types.h"
#include "riscv.h"
#include "ksm.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"

extern struct proc proc[NPROC];

struct merged_page_list mlist[NMLIST];
void *zeropage = 0;
uint64 zp_refcnt = 0;   //zeropage reference count
uint64 zp_hash = 0;     //zeropage hash

static int scanned;
static int merged;

static void
set_zeropage(void)
{
  zeropage = kalloc();
  if (!zeropage)
    panic("zeropage mappgng");
  memset(zeropage, 0, PGSIZE);
  zp_refcnt = 0;
  zp_hash = xxh64(zeropage, PGSIZE);
}

// merge zeropage
// return 1 when merged zeropage
// return 0 when not zeropage
// return -1 when already merged zeropage
static int
merge_zeropage(pte_t *pte, uint64 hash)
{
  // if already merged zeropage
  if ((void *)PTE2PA(*pte) == zeropage)
    return -1;

  // if zeropage
  if (hash == zp_hash)
  {
    kfree((void *)PTE2PA(*pte));
    *pte = PA2PTE(zeropage) | (PTE_FLAGS(*pte) & ~PTE_W); // disable write
    ++zp_refcnt;
    ++merged;
    return 1;
  }
  return 0;
}

static void
print_mlist(void)
{
  printf("---mlist---\n");
  printf("zeropage : pa->%p, refcnt->%d, hash->%d\n", zeropage, zp_refcnt, zp_hash);

  for (int i = 0; i < NMLIST; i++)
  {
    if (mlist[i].refcnt != 0)
      printf("mlist [%d]: pa->%p, refcnt->%d, hash->%d\n", i, mlist[i].pa, mlist[i].refcnt, mlist[i].hash);
  }
  printf("---mlist end---\n");
}

static void
update_mlist()
{
  for (int i = 0; i < NMLIST; i++)
  {
    if (mlist[i].refcnt == 1)
      mlist[i].hash = xxh64((void *)mlist[i].pa, PGSIZE);
  }
}

static void
push_mlist(pte_t *pte, uint64 hash)
{
  for (int i = 0; i < NMLIST; i++)
  {
    if (mlist[i].refcnt > 0 && mlist[i].pa == PTE2PA(*pte)) // if already merged page
     return;
  }

  for (int i = 0; i < NMLIST; i++)
  {
    if (mlist[i].refcnt > 0 && mlist[i].hash == hash && mlist[i].pa != PTE2PA(*pte))
    {
      if (mlist[i].refcnt == 1 && mlist[i].pte != 0)
        *mlist[i].pte = *mlist[i].pte & ~PTE_W; // disable write flag when merging
      printf("merging %p to %p <-mlist[%d]\n", PTE2PA(*pte), mlist[i].pa, i);

      kfree((void *)PTE2PA(*pte));
      *pte = PA2PTE(mlist[i].pa) | (PTE_FLAGS(*pte) & ~PTE_W);
      mlist[i].refcnt++;
      return ;
    }
  }

  for (int i = 0; i < NMLIST; i++)
  {
    if (mlist[i].refcnt == 0)
    {
      mlist[i].pte = pte;
      mlist[i].pa = PTE2PA(*pte);
      ++(mlist[i].refcnt);
      mlist[i].hash = hash;
      break;
    }
  }
}

static void
merge_page(pte_t *pte, uint64 hash)
{
  if (merge_zeropage(pte, hash) == 0)
    push_mlist(pte, hash);
}

uint64
sys_ksm(void)
{
  printf("ksm()\n");
  struct proc *pr = myproc();
  struct proc *p;
  pte_t *pte;
  uint64 va0;
  uint64 scanned_usraddr;
  uint64 merged_usraddr;
  uint64 hash;

  argaddr(0, &scanned_usraddr);
  argaddr(1, &merged_usraddr);
  scanned = 0;
  merged = 0;

  // if zeropage is not set, set zeropage
  if (zeropage == 0)
    set_zeropage();

  update_mlist();

  // per process traverse
  for (p = proc; p < &proc[NPROC]; p++)
  {
    // not merging pid 1, pid 2, invalid process, called process
    if (p->pid <= 2 || p->pid == pr->pid)
      continue;
    if (p->state == UNUSED && p->state == ZOMBIE)
      continue;

    va0 = 0;
    while (va0 < p->sz)
    {
      pte = walk(p->pagetable, va0, 0);

      // not valid PTE
      if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U))
      {
        va0 += PGSIZE;
        continue;
      }

      hash = xxh64((void *)PTE2PA(*pte), PGSIZE);
      ++scanned;

      merge_page(pte, hash);
      printf("scanned pid: %d, va0: %p, pa0: %p, xxh: %d\n", p->pid, va0, PTE2PA(*pte), hash);

      va0 += PGSIZE;
    }
  }

  // write out scanned and merged to user space
  copyout(pr->pagetable, scanned_usraddr, (char *)&scanned, 4);
  copyout(pr->pagetable, merged_usraddr, (char *)&merged, 4);
  print_mlist();

  printf("ksm() end\n");
  return freemem;
}

