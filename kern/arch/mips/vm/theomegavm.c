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

// The last VM you'll ever need.

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

// startaddr, freeaddr is the coremap. freeaddr, endaddr is the
// coremap.
static paddr_t startaddr, endaddr, freeaddr;

static struct Page *coremap;

static int bootstrap = 0;

static unsigned long long num_pages;

// Setup Mon flying coremap, map, map, map, map.
// The next free page is at curfreeaddr + PAGE_SIZE.
void vm_bootstrap(void) {

  unsigned long long i;

  ram_getsize(&startaddr, &endaddr);

  num_pages = (endaddr - startaddr) / PAGE_SIZE;

  freeaddr = startaddr + num_pages * sizeof(struct Page);
  freeaddr = ROUNDUP(freeaddr, PAGE_SIZE);

  // Setup the coremap.
  coremap = (struct Page *)PADDR_TO_KVADDR(startaddr);
  for (i = 0; i < num_pages; i++) {
    coremap[i].addrspace = NULL;
    coremap[i].paddr = freeaddr + PAGE_SIZE * i;
    coremap[i].vaddr = PADDR_TO_KVADDR(coremap[i].paddr);
    coremap[i].state = 0;
    coremap[i].timestamp = 0; // For now. Change this later.
    coremap[i].pagecount = 0;

    //bzero((void *)coremap[i].vaddr, PAGE_SIZE);
  }

  bootstrap = 1;
}

static paddr_t getppages(unsigned long npages) {

  paddr_t newaddr;
  int flag;
  unsigned long count, i, index;


  if (bootstrap == 0) {
    newaddr = ram_stealmem(npages);
    return newaddr;
  }

  spinlock_acquire(&stealmem_lock);

  flag = 0; count = 0; index = 0;
  for (i = 0; i < num_pages; i++) {

    if (count == npages) {
      break;
    }

    if (coremap[i].state == 0) {
      if (flag == 0) {
        index = i;
        flag = 1;
      }
      count++;
    }
    else if (coremap[i].state == 1) {
      flag = 0;
      count = 0;
    }

  }

  if (count != npages) {
    return 0;
  }

  coremap[index].pagecount = npages;
  newaddr = coremap[index].paddr;

  for (i = index; i < index + npages; i++) {
    coremap[i].state = 1; // Update addrspace. Update timestamp.
    bzero((void *)coremap[i].vaddr, PAGE_SIZE);
  }

  spinlock_release(&stealmem_lock);
  return newaddr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t alloc_kpages(int npages)
{
  paddr_t pa;

  pa = getppages(npages);

  if (pa == 0) {
    return 0;
  }

  return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
  unsigned long long i, j;

  for (i = 0; i < num_pages; i++) {
    if (coremap[i].vaddr == addr) {
      //for (j = 0; j < coremap[i].pagecount; j++) {
        j = 0;
        bzero((void *)coremap[i + j].vaddr, PAGE_SIZE);
        coremap[i + j].state = 0;
        coremap[i + j].addrspace = NULL;
        // update timestamp here.
        coremap[i + j].pagecount = 0;
      //}
      break;
    }
  }
  (void)addr;

}

void
vm_tlbshootdown_all(void)
{
  panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
  (void)ts;
  panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
  vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
  paddr_t paddr;
  int i;
  uint32_t ehi, elo;
  struct addrspace *as;
  int spl;

  faultaddress &= PAGE_FRAME;

  DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

  switch (faulttype) {
      case VM_FAULT_READONLY:
    // We always create pages read-write, so we can't get this
    panic("dumbvm: got VM_FAULT_READONLY\n");
      case VM_FAULT_READ:
      case VM_FAULT_WRITE:
    break;
      default:
    return EINVAL;
  }

  as = curthread->t_addrspace;
  if (as == NULL) {

     // No address space set up. This is probably a kernel
     // fault early in boot. Return EFAULT so as to panic
     // instead of getting into an infinite faulting loop.
    return EFAULT;
  }

  // Assert that the address space has been set up properly.
  KASSERT(as->as_vbase1 != 0);
  KASSERT(as->as_pbase1 != 0);
  KASSERT(as->as_npages1 != 0);
  KASSERT(as->as_vbase2 != 0);
  KASSERT(as->as_pbase2 != 0);
  KASSERT(as->as_npages2 != 0);
  KASSERT(as->as_stackpbase != 0);
  KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
  KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
  KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
  KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
  KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

  vbase1 = as->as_vbase1;
  vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
  vbase2 = as->as_vbase2;
  vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
  stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
  stacktop = USERSTACK;

  if (faultaddress >= vbase1 && faultaddress < vtop1) {
    paddr = (faultaddress - vbase1) + as->as_pbase1;
  }
  else if (faultaddress >= vbase2 && faultaddress < vtop2) {
    paddr = (faultaddress - vbase2) + as->as_pbase2;
  }
  else if (faultaddress >= stackbase && faultaddress < stacktop) {
    paddr = (faultaddress - stackbase) + as->as_stackpbase;
  }
  else {
    return EFAULT;
  }

  // make sure it's page-aligned
  KASSERT((paddr & PAGE_FRAME) == paddr);

  // Disable interrupts on this CPU while frobbing the TLB.
  spl = splhigh();

  for (i=0; i<NUM_TLB; i++) {
    tlb_read(&ehi, &elo, i);
    if (elo & TLBLO_VALID) {
      continue;
    }
    ehi = faultaddress;
    elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
    DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
    tlb_write(ehi, elo, i);
    splx(spl);
    return 0;
  }

  kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
  splx(spl);
  return EFAULT;
}

/*struct addrspace *
as_create(void)
{
  struct addrspace *as = kmalloc(sizeof(struct addrspace));
  if (as==NULL) {
    return NULL;
  }

  as->as_vbase1 = 0;
  as->as_pbase1 = 0;
  as->as_npages1 = 0;
  as->as_vbase2 = 0;
  as->as_pbase2 = 0;
  as->as_npages2 = 0;
  as->as_stackpbase = 0;

  return as;
}

void
as_destroy(struct addrspace *as)
{
  kfree(as);
}

void
as_activate(struct addrspace *as)
{
  int i, spl;

  (void)as;

  // Disable interrupts on this CPU while frobbing the TLB.
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

  // Support for more than two regions is not available.
  kprintf("dumbvm: Warning: too many regions\n");
  return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
  bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
  KASSERT(as->as_pbase1 == 0);
  KASSERT(as->as_pbase2 == 0);
  KASSERT(as->as_stackpbase == 0);

  as->as_pbase1 = getppages(as->as_npages1);
  if (as->as_pbase1 == 0) {
    return ENOMEM;
  }

  as->as_pbase2 = getppages(as->as_npages2);
  if (as->as_pbase2 == 0) {
    return ENOMEM;
  }

  as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
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

  // (Mis)use as_prepare_load to allocate some physical memory.
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
}*/