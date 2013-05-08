/*
 * You can haz MIT GPL lisence.
 *
 */

#include <addrspace.h>

#ifndef _VM_H_
#define _VM_H_

/*
 * VM system-related definitions.
 *
 * You'll probably want to add stuff here.
 */

#include <machine/vm.h>

/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/

// Page state variable for clean code. Thanks Jhishi.
typedef enum {FREE, DIRTY, CLEAN, FIXED} pagestate_t;

// Under dumbvm, and we are for a while,
// always have 48k of user stack.
#define DUMBVM_STACKPAGES    12

struct Page {

  struct addrspace *addrspace;

  vaddr_t vaddr;
  paddr_t paddr;

  // Much cleaner.
  pagestate_t state;

  time_t timestamp;

  unsigned long long pagecount;
};

/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

// Inner Functions
paddr_t getppages(unsigned long npages, int state);
void freeppages(vaddr_t addr);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(int npages);
void free_kpages(vaddr_t addr);

// Stuff for user functions.
vaddr_t alloc_upages(int npages);
void free_upages(vaddr_t addr);

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown_all(void);
void vm_tlbshootdown(const struct tlbshootdown *);


#endif /* _VM_H_ */
