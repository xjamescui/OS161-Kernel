#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <thread.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace * as_create(void) {

  struct addrspace *as = kmalloc(sizeof(struct addrspace));
  if (as == NULL) {
    return NULL;
  }

  as->regionlisthead = NULL;

  as->pagetable = NULL;

  as->stackpbase = 0;

  return as;
}

void as_destroy(struct addrspace *as) {

  struct pagetable *ptnode, *pttemp;
  struct regionlistnode *rlnode, *rltemp;

  // loop and delete the page table list.
  ptnode = as->pagetable;
  while (ptnode != NULL) {
    pttemp = ptnode;
    ptnode = ptnode->next;
    kfree(pttemp);
  }

  // loop and delete the region list.
  rlnode = as->regionlisthead;
  while(rlnode != NULL) {
    rltemp = rlnode;
    rlnode = rlnode->next;
    kfree(rltemp);
  }

  kfree(as);
}

void as_activate(struct addrspace *as) {

  int i, spl;

  // Don't do much about this it right now.
  (void)as;

  // Disable interrupts on this CPU while frobbing the TLB.
  spl = splhigh();

  for (i=0; i<NUM_TLB; i++) {
    tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
  }

  splx(spl);
}

int as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz, int readable, int writeable, int executable) {

  size_t npages;
  struct regionlistnode *rlnode;

  // Align the region. First, the base...
  sz += vaddr & ~(vaddr_t)PAGE_FRAME;
  vaddr &= PAGE_FRAME;

  // ...and now the length.
  sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

  npages = sz / PAGE_SIZE;

  // We don't use these - all pages are read-write
  (void)readable;  (void)writeable;  (void)executable;

  // Walk through the region list and add this guy.
  if (as->regionlisthead == NULL) {
    as->regionlisthead = kmalloc(sizeof(struct regionlistnode));
    as->regionlisthead->next = NULL;
    rlnode = as->regionlisthead;
  }
  else {
    rlnode = as->regionlisthead;
    while (rlnode->next != NULL) {
      rlnode = rlnode->next;
    }
    rlnode->next = kmalloc(sizeof(struct regionlistnode));
    rlnode->next->next = NULL;
    rlnode = rlnode->next;
  }

  rlnode->vbase = vaddr;
  rlnode->npages = npages;
  rlnode->pbase = 0;

  return 0;
}

void as_zero_region(paddr_t paddr, unsigned npages) {
  bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int as_prepare_load(struct addrspace *as) {

  struct regionlistnode *rlnode;

  struct pagetable *pagetableentry;
  vaddr_t vbase;
  int count;

  if (as->pagetable == NULL) {
    as->pagetable = kmalloc(sizeof(struct pagetable));
    as->pagetable->vaddr = 0; as->pagetable->paddr = 0; as->pagetable->next = NULL;
  }

  pagetableentry = as->pagetable;
  rlnode = as->regionlisthead;
  while (1) {

    vbase = rlnode->vbase;
    count = rlnode->npages;
    while (1) {
      pagetableentry->vaddr = vbase;
      pagetableentry->paddr = alloc_upages(1);
      pagetableentry->next = NULL;
      as_zero_region(pagetableentry->paddr, 1);

      count--;
      if (count == 0)
        break;

      pagetableentry->next = kmalloc(sizeof(struct pagetable)); pagetableentry->next->next = NULL;
      pagetableentry = pagetableentry->next;
      vbase += PAGE_SIZE;
    }
    rlnode = rlnode->next;
    if (rlnode == NULL)
      break;
    pagetableentry->next = kmalloc(sizeof(struct pagetable)); pagetableentry->next->next = NULL;
    pagetableentry = pagetableentry->next;
  }


  KASSERT(as->stackpbase == 0);
  as->stackpbase = alloc_upages(DUMBVM_STACKPAGES);
  if (as->stackpbase == 0) {
    return ENOMEM;
  }
  as_zero_region(as->stackpbase, DUMBVM_STACKPAGES);

  return 0;
}

int as_complete_load(struct addrspace *as) {

  (void)as;
  return 0;
}

int as_define_stack(struct addrspace *as, vaddr_t *stackptr) {

  KASSERT(as->stackpbase != 0);
  (void)as;

  *stackptr = USERSTACK;
  return 0;
}

int as_copy(struct addrspace *old, struct addrspace **ret) {

  struct addrspace *new;
  struct regionlistnode *rlnew, *rlold;

  new = as_create();
  if (new == NULL) {
    return ENOMEM;
  }

  new->regionlisthead = kmalloc(sizeof(struct regionlistnode));
  new->regionlisthead->next = NULL;

  rlnew = new->regionlisthead;
  rlold = old->regionlisthead;
  while (1) {
    rlnew->vbase = rlold->vbase;
    rlnew->npages = rlold->npages;
    rlnew->pbase = 0;

    rlold = rlold->next;
    if (rlold == NULL)
      break;

    rlnew->next = kmalloc(sizeof(struct regionlistnode));
    rlnew->next->next = NULL;
    rlnew = rlnew->next;
  }

  // (Mis)use as_prepare_load to allocate some physical memory.
  if (as_prepare_load(new)) {
    as_destroy(new);
    return ENOMEM;
  }

  KASSERT(new->stackpbase != 0);
  memmove((void *)PADDR_TO_KVADDR(new->stackpbase), (const void *)PADDR_TO_KVADDR(old->stackpbase), DUMBVM_STACKPAGES*PAGE_SIZE);


  struct pagetable *newpagetable, *oldpagetable;

  KASSERT(newpagetable != NULL);
  newpagetable = new->pagetable;
  oldpagetable = old->pagetable;

  while (1) {

    // This may leak memory.
    newpagetable->vaddr = oldpagetable->vaddr;
    memmove((void *)PADDR_TO_KVADDR(newpagetable->paddr), (const void *)PADDR_TO_KVADDR(oldpagetable->paddr), PAGE_SIZE);

    if (oldpagetable->next == NULL)
      break;

    newpagetable = newpagetable->next;
    oldpagetable = oldpagetable->next;
  }

  *ret = new;
  return 0;
}
