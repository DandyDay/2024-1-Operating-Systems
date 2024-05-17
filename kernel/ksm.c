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
struct merged_page_list zeropage;

// static global variable used for sys_ksm() return
static int scanned;
static int merged;

// static void
// init_zeropage(void)
// {
//   zeropage.pa = (uint64)kalloc();
//   if (!zeropage.pa)
//     panic("zeropage mappgng");
//   memset((void *)zeropage.pa, 0, PGSIZE);
//   zeropage.pte = 0;
//   zeropage.refcnt = 0;
//   zeropage.hash = xxh64((void *)zeropage.pa, PGSIZE);
// }

// merge zeropage
// return 1 when merged zeropage
// return 0 when not zeropage
// return -1 when already merged zeropage
static int
merge_zeropage(pte_t *pte, uint64 hash)
{
  // if already merged zeropage
  if (PTE2PA(*pte) == zeropage.pa)
    return -1;

  // if zeropage, MERGE
  if (hash == zeropage.hash)
  {
    kfree((void *)PTE2PA(*pte));
    if (*pte & PTE_W)
      *pte = PA2PTE(zeropage.pa) | ((PTE_FLAGS(*pte) & ~PTE_W) | (1L << 8)); // disable write
    else
      *pte = PA2PTE(zeropage.pa) | (PTE_FLAGS(*pte) & ~PTE_W); // disable write

    ++(zeropage.refcnt);
    return 1;
  }
  return 0;
}

// void
// print_mlist(void)
// {
//   printf("---mlist---\n");
//   printf("zeropage : pa->%p, refcnt->%d, hash->%d\n", zeropage, zeropage.refcnt, zeropage.hash);

//   for (int i = 0; i < NMLIST; i++)
//   {
//     if (mlist[i].refcnt > 0)
//     {
//       printf("mlist [%d]: pa->%p, refcnt->%d, hash->%d\n", i, mlist[i].pa, mlist[i].refcnt, mlist[i].hash);
//     }
//   }
//   printf("---mlist end---\n");
// }

void
update_mlist()
{
  for (int i = 0; i < NMLIST; i++)
  {
    if (mlist[i].refcnt == 1 && !mlist[i].merged)
      memset(&mlist[i], 0, sizeof(struct merged_page_list));
  }
}

// return 1 when merged
// return 0 when not merged
static int
push_mlist(pte_t *pte, uint64 hash)
{
  for (int i = 0; i < NMLIST; i++)
  {
    if (mlist[i].refcnt > 0 && mlist[i].pa == PTE2PA(*pte)) // if already merged page
     return 0;
  }

  for (int i = 0; i < NMLIST; i++)
  {
    // if merge available, MERGE
    if (mlist[i].refcnt > 0 && mlist[i].refcnt < 16 && mlist[i].hash == hash && mlist[i].pa != PTE2PA(*pte))
    {
      if (mlist[i].refcnt == 1 && mlist[i].pte != 0)
      {
        if (*mlist[i].pte & PTE_W || *mlist[i].pte & (1L << 8)) // if has write permission
          *mlist[i].pte = (*mlist[i].pte & ~PTE_W) | (1L << 8);
        else // doesn't have write permission
          *mlist[i].pte = *mlist[i].pte & ~PTE_W; // disable write flag when merging
        mlist[i].merged = 1;
      }
      // printf("merging %p to %p <-mlist[%d]\n", PTE2PA(*pte), mlist[i].pa, i);

      kfree((void *)PTE2PA(*pte));

      if (*pte & PTE_W)
        *pte = PA2PTE(mlist[i].pa) | ((PTE_FLAGS(*pte) & ~PTE_W) | (1L << 8));
      else
        *pte = PA2PTE(mlist[i].pa) | (PTE_FLAGS(*pte) & ~PTE_W);

      mlist[i].refcnt++;
      return 1;
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
      mlist[i].merged = 0;
      break;
    }
  }
  return 0;
}

static int
merge_page(pte_t *pte, uint64 hash)
{
  int is_merged = 0;

  is_merged = merge_zeropage(pte, hash);
  if (is_merged == 1) // zeropage merged
    return 1;
  else if (is_merged == 0)  // not zeropage, so try to merge
    return push_mlist(pte, hash);
  else  // already merged zeropage
    return 0;
}

uint64
sys_ksm(void)
{
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

  // // if zeropage is not set, init zeropage
  // if (zeropage.pa == 0)
  //   init_zeropage();


  // per process traverse
  for (p = proc; p < &proc[NPROC]; p++)
  {
    // not merging pid 1, pid 2, invalid process, called process
    if (p->pid <= 2 || p->pid == pr->pid)
      continue;

    va0 = 0;
    while (va0 < p->sz)
    {
      pte = walk(p->pagetable, va0, 0);

      // not valid PTE
      if (!pte || !(*pte & PTE_V))
      {
        va0 += PGSIZE;
        continue;
      }

      hash = xxh64((void *)PTE2PA(*pte), PGSIZE);
      ++scanned;

      merged += merge_page(pte, hash);
      // printf("scanned pid: %d, va0: %p, pa0: %p, xxh: %d\n", p->pid, va0, PTE2PA(*pte), hash);

      va0 += PGSIZE;
    }
  }
  // delete not shared element
  update_mlist();

  // write out scanned and merged to user space
  copyout(pr->pagetable, scanned_usraddr, (char *)&scanned, sizeof(int));
  copyout(pr->pagetable, merged_usraddr, (char *)&merged, sizeof(int));
  // print_mlist();

  return freemem;
}

