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

// #ifdef SNU


extern int freemem;

uint64 xxh64(void *input, unsigned int len);

void update_mlist();

struct merged_page_list {
  pte_t  *pte;
  uint64 pa;
  uint64 hash;
  uint32 refcnt;
  uint32 merged;
};

extern struct merged_page_list zeropage;
extern struct merged_page_list mlist[NMLIST];

// #endif
