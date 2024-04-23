#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");

	printf("SNUOS2024\n");
	printf(
		"\x1b[34m _  _  _  _\e[0m      \x1b[31m, _  __  ___ _ \e[0m\n"
		"\x1b[34m/ )/ \\/ )/ \\\e[0m    \x1b[31m/|/ )(__)|__ / )\e[0m\n"
		"\x1b[34m /|   |/|   |\e[0m----\x1b[31m| / /  \\   \\ / \e[0m\n"
		"\x1b[34m/__\\_//__\\_/\e[0m     \x1b[31m|/__\\__/\\__//__\e[0m\n"
	);
	printf(
		"\e[1;34mMMMMMMMM\"\"M\e[0m oo          dP                   \e[1;34mMM'\"\"\"\"'YMM\e[0m dP                oo\n"
		"\e[1;34mMMMMMMMM  M\e[0m             88                   \e[1;34mM' .mmm. `M\e[0m 88                  \n"
		"\e[1;34mMMMMMMMM  M\e[0m dP 88d888b. 88d888b. .d8888b.    \e[1;34mM  MMMMMooM\e[0m 88d888b. .d8888b. dP\n"
		"\e[1;34mMMMMMMMM  M\e[0m 88 88'  `88 88'  `88 88'  `88    \e[1;34mM  MMMMMMMM\e[0m 88'  `88 88'  `88 88\n"
		"\e[1;34mM. `MMM' .M\e[0m 88 88    88 88    88 88.  .88    \e[1;34mM. `MMM' .M\e[0m 88    88 88.  .88 88\n"
		"\e[1;34mMM.     .MM\e[0m dP dP    dP dP    dP `88888P'    \e[1;34mMM.     .dM\e[0m dP    dP `88888P' dP\n"
		"\e[1;34mMMMMMMMMMMM\e[0m                                  \e[1;34mMMMMMMMMMMM\e[0m                     \n"
	);
    printf("\n");

    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    userinit();      // first user process
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();
}
