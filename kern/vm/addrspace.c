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

  /*as->regionlisthead = kmalloc(sizeof(struct regionlistnode));
  if (as->regionlisthead == NULL) {
    kfree(as);
    return NULL;
  }
  as->regionlisthead->vbase = 0;
  as->regionlisthead->pbase = 0;
  as->regionlisthead->npages = 0;
  as->regionlisthead->next = NULL;*/
  as->regionlisthead = NULL;

  as->pagetable = kmalloc(sizeof(struct pagetable));
  as->pagetable->next = NULL;

  as->stackpbase = 0;

//////////////////////////////////////////////////////

  as->as_vbase1 = 0;
  as->as_pbase1 = 0;
  as->as_npages1 = 0;
  as->as_vbase2 = 0;
  as->as_pbase2 = 0;
  as->as_npages2 = 0;
  as->as_stackpbase = 0;

//////////////////////////////////////////////////////

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

// Set up a segment at virtual address VADDR of size MEMSIZE. The
// segment in memory extends from VADDR up to (but not including)
// VADDR+MEMSIZE.
// The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
// write, or execute permission should be set on the segment. At the
// moment, these are ignored. When you write the VM system, you may
// want to implement them.
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
  (void)readable;
  (void)writeable;
  (void)executable;

  // Walk through the region list and add this guy.
  rlnode = as->regionlisthead;
  if (rlnode == NULL) {
    as->regionlisthead = kmalloc(sizeof(struct regionlistnode));
    as->regionlisthead->vbase = vaddr;
    as->regionlisthead->npages = npages;
    as->regionlisthead->pbase = 0;
    as->regionlisthead->next = NULL;
  }
  else {
    while (rlnode->next != NULL) {
      rlnode = rlnode->next;
    }
    rlnode->next = kmalloc(sizeof(struct regionlistnode));
    if (rlnode->next == NULL) {
      return ENOMEM;
    }

    rlnode->next->vbase = vaddr;
    rlnode->next->npages = npages;
    rlnode->next->pbase = 0;
    rlnode->next->next = NULL;
  }

  if (as->as_vbase1 == 0) {
    as->as_vbase1 = vaddr;
    as->as_npages1 = npages;
    return 0;
  }

  if (as->as_vbase2 == 0) {
    as->as_vbase2 = vaddr;
    as->as_npages2 = npages;
    return 0;
  }

  return 0;
}

void as_zero_region(paddr_t paddr, unsigned npages) {
  bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int as_prepare_load(struct addrspace *as) {

  struct regionlistnode *rlnode;

  KASSERT(as->as_pbase1 == 0);
  KASSERT(as->as_pbase2 == 0);
  KASSERT(as->as_stackpbase == 0);
  as->as_pbase1 = getppages(as->as_npages1, DIRTY);
  if (as->as_pbase1 == 0) {
    return ENOMEM;
  }
  as->as_pbase2 = getppages(as->as_npages2, DIRTY);
  if (as->as_pbase2 == 0) {
    return ENOMEM;
  }
  as->as_stackpbase = getppages(DUMBVM_STACKPAGES, DIRTY);
  if (as->as_stackpbase == 0) {
    return ENOMEM;
  }
  as_zero_region(as->as_pbase1, as->as_npages1);
  as_zero_region(as->as_pbase2, as->as_npages2);
  as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);


  rlnode = as->regionlisthead;
  while (rlnode != NULL) {

    KASSERT(rlnode->pbase == 0);
    rlnode->pbase = alloc_upages(rlnode->npages);
    if (rlnode->pbase == 0) {
      return ENOMEM;
    }
    as_zero_region(rlnode->pbase, rlnode->npages);
    rlnode = rlnode->next;
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

  KASSERT(as->as_stackpbase != 0);

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

  rlold = old->regionlisthead;
  rlnew = new->regionlisthead;
  while (1) {

    rlnew->vbase = rlold->vbase;
    rlnew->npages = rlold->npages;
    rlnew->pbase = 0;

    rlold = rlold->next;
    if (rlold == NULL) {
      break;
    }

    if (rlnew->next == NULL) {
      rlnew->next = kmalloc(sizeof(struct regionlistnode));
      rlnew->next->next = NULL;
      rlnew = rlnew->next;
    }
  }

  new->as_vbase1 = old->as_vbase1;
  new->as_npages1 = old->as_npages1;
  new->as_vbase2 = old->as_vbase2;
  new->as_npages2 = old->as_npages2;

  /* (Mis)use as_prepare_load to allocate some physical memory. */
  if (as_prepare_load(new)) {
    as_destroy(new);
    return ENOMEM;
  }

  KASSERT(new->stackpbase != 0);
  memmove((void *)PADDR_TO_KVADDR(new->stackpbase), (const void *)PADDR_TO_KVADDR(old->stackpbase), DUMBVM_STACKPAGES*PAGE_SIZE);

  rlnew = new->regionlisthead;
  rlold = new->regionlisthead;
  while (rlnew != NULL) {

    KASSERT(rlnew->pbase != 0);
    memmove((void *)PADDR_TO_KVADDR(rlnew->pbase), (const void *)PADDR_TO_KVADDR(rlold->pbase), rlold->npages*PAGE_SIZE);

    rlnew = rlnew->next;
    rlold = rlold->next;
  }

//////////////////////////////////

  KASSERT(new->as_pbase1 != 0);
  KASSERT(new->as_pbase2 != 0);
  KASSERT(new->as_stackpbase != 0);

  memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
    (const void *)PADDR_TO_KVADDR(old->as_pbase1),
    old->as_npages1*PAGE_SIZE);

  memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
    (const void *)PADDR_TO_KVADDR(old->as_pbase2),
    old->as_npages2*PAGE_SIZE);

  memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
    (const void *)PADDR_TO_KVADDR(old->as_stackpbase),
    DUMBVM_STACKPAGES*PAGE_SIZE);

///////////////////////////////////////////////////////////////////
  *ret = new;
  return 0;
}
