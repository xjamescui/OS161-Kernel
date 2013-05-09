#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <mips/tlb.h>
#include <vm.h>
#include <addrspace.h>
#include <synch.h>

struct addrspace * as_create(void) {

  struct addrspace *as = kmalloc(sizeof(struct addrspace));
  if (as == NULL) {
    return NULL;
  }

  as->as_vbase1 = 0;
  as->as_pbase1 = 0;
  as->as_npages1 = 0;
  as->as_vbase2 = 0;
  as->as_pbase2 = 0;
  as->as_npages2 = 0;
  as->as_stackpbase = 0;

  as->pagetable = kmalloc(sizeof(struct pagetable));
  if (as->pagetable == NULL) {
    kfree(as);
    return NULL;
  }

  return as;
}

void as_destroy(struct addrspace *as) {

  struct pagetable *node, *temp;

  kfree(as);

  // loop and delete the linkedlist.
  node = as->pagetable;
  while (node != NULL) {
    temp = node;
    node = node->next;
    kfree(temp);
  }
}

void as_activate(struct addrspace *as) {

  int i, spl;

  (void)as;

  /* Disable interrupts on this CPU while frobbing the TLB. */
  spl = splhigh();

  for (i=0; i<NUM_TLB; i++) {
    tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
  }

  splx(spl);
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
     int readable, int writeable, int executable)
{
  size_t npages; 

  /* Align the region. First, the base... */
  sz += vaddr & ~(vaddr_t)PAGE_FRAME;
  vaddr &= PAGE_FRAME;

  /* ...and now the length. */
  sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

  npages = sz / PAGE_SIZE;

  /* We don't use these - all pages are read-write */
  (void)readable;
  (void)writeable;
  (void)executable;

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

  /*
   * Support for more than two regions is not available.
   */
  kprintf("dumbvm: Warning: too many regions\n");
  return EUNIMP;
}

void as_zero_region(paddr_t paddr, unsigned npages) {
  bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
  KASSERT(as->as_pbase1 == 0);
  KASSERT(as->as_pbase2 == 0);
  KASSERT(as->as_stackpbase == 0);

  as->as_pbase1 = alloc_upages(as->as_npages1);
  if (as->as_pbase1 == 0) {
    return ENOMEM;
  }

  as->as_pbase2 = alloc_upages(as->as_npages2);
  if (as->as_pbase2 == 0) {
    return ENOMEM;
  }

  as->as_stackpbase = alloc_upages(DUMBVM_STACKPAGES);
  if (as->as_stackpbase == 0) {
    return ENOMEM;
  }

  as_zero_region(as->as_pbase1, as->as_npages1);
  as_zero_region(as->as_pbase2, as->as_npages2);
  as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

  return 0;
}

int
as_complete_load(struct addrspace *as)
{
  (void)as;
  return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
  KASSERT(as->as_stackpbase != 0);

  *stackptr = USERSTACK;
  return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
  struct addrspace *new;

  new = as_create();
  if (new==NULL) {
    return ENOMEM;
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
  
  *ret = new;
  return 0;
}



















/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

/*struct addrspace * as_create(void) {

  struct addrspace *as;

  // My stuff got put here.

  as->as_vbase1 = 0;
  as->as_pbase1 = 0;
  as->as_npages1 = 0;
  as->as_vbase2 = 0;
  as->as_pbase2 = 0;
  as->as_npages2 = 0;
  as->as_stackpbase = 0;

  as->pagetablehead = alloc_upages(1);

  // My stuff ended.

  return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{

  struct addrspace *newas;

  newas = as_create();
  if (newas==NULL) {
    return ENOMEM;
  }

  // My stuff got put here

  (void)old;

  // My stuff ended.

  *ret = newas;
  return 0;
}

void
as_destroy(struct addrspace *as)
{
  // Clean up as needed.

  kfree(as);
}

void
as_activate(struct addrspace *as)
{
  // Write this.

  (void)as;

}*/

 /*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */

/*int as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz, int readable, int writeable, int executable) {

  // Write this.
  (void)as;
  (void)vaddr;
  (void)sz;
  (void)readable;
  (void)writeable;
  (void)executable;
  return EUNIMP;

  return 0;
}

//static void as_zero_region(paddr_t paddr, unsigned npages) {
  bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int as_prepare_load(struct addrspace *as) {

  // Write this.

  (void)as;
  return 0;
}

int as_complete_load(struct addrspace *as) {

  // Write this.
  (void)as;
  return 0;
}

int as_define_stack(struct addrspace *as, vaddr_t *stackptr) {

  struct addrspace *new;

  new = as_create();
  if (new == NULL) {
    return ENOMEM;
  }

  // Write this.

  (void) as;

  // Initial user-level stack pointer
  *stackptr = USERSTACK;

  return 0;
}*/
