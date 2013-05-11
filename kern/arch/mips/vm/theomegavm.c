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
#include <synch.h>

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

static struct lock *coremaplock;

// Setup Mon flying coremap, map, map, map, map.
// The next free page is at curfreeaddr + PAGE_SIZE.
void vm_bootstrap(void) {

  unsigned long long i;

  ram_getsize(&startaddr, &endaddr);

  num_pages = (endaddr - startaddr) / PAGE_SIZE;

  freeaddr = startaddr + num_pages * sizeof(struct Page);
  freeaddr = ROUNDUP(freeaddr, PAGE_SIZE);

  KASSERT((freeaddr & PAGE_FRAME) == freeaddr);

  // Setup the coremap.
  coremap = (struct Page *)PADDR_TO_KVADDR(startaddr);
  for (i = 0; i < num_pages; i++) {
    coremap[i].addrspace = NULL;
    coremap[i].paddr = freeaddr + PAGE_SIZE * i;
    coremap[i].vaddr = PADDR_TO_KVADDR(coremap[i].paddr);
    coremap[i].state = FREE;
    coremap[i].timestamp = 0; // For now. Change this later.
    coremap[i].pagecount = 0;
  }

  coremaplock = lock_create("Coremap Lock");

  bootstrap = 1;
}

paddr_t getppages(unsigned long npages, int state) {

  paddr_t newaddr;
  int flag;
  unsigned long count, i, index, j;


  if (bootstrap == 0) {
    newaddr = ram_stealmem(npages);
    return newaddr;
  }

  spinlock_acquire(&stealmem_lock);
  //lock_acquire(coremaplock);

  flag = 0; count = 0; index = 0; j = 0;
  for (i = 0; i < num_pages; i++) {

    if (count == npages) {
      break;
    }

    if (coremap[i].state == FREE) {
      if (flag == 0) {
        index = i;
        flag = 1;
      }
      count++;
    }
    else if (coremap[i].state == DIRTY || coremap[i].state == CLEAN || coremap[i].state == FIXED) {
      flag = 0;
      count = 0;
    }

  }

  if (count != npages) {
    // Perform magic here rather that returning zero.
    return 0;
  }

  coremap[index].pagecount = npages;
  newaddr = coremap[index].paddr;

  for (i = index; i < index + npages; i++) {
    coremap[i].state = state; // Update addrspace. Update timestamp.
    bzero((void *)coremap[i].vaddr, PAGE_SIZE);
  }

  spinlock_release(&stealmem_lock);
  //lock_release(coremaplock);

  return newaddr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t alloc_kpages(int npages)
{
  paddr_t pa;

  pa = getppages(npages, FIXED);

  if (pa == 0) {
    return 0;
  }

  return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr) {
  unsigned long long i, j;

  spinlock_acquire(&stealmem_lock);
  //lock_acquire(coremaplock);

  for (i = 0; i < num_pages; i++) {
    if (coremap[i].vaddr == addr) {
      //for (j = 0; j < coremap[i].pagecount; j++) {
        j = 0;
        bzero((void *)coremap[i + j].vaddr, PAGE_SIZE);
        coremap[i + j].state = FREE;
        coremap[i + j].addrspace = NULL;
        // update timestamp here.
        coremap[i + j].pagecount = 0;
      //}
      break;
    }
  }

  (void)addr;

  spinlock_release(&stealmem_lock);
  //lock_release(coremaplock);
}

// User pages interface to coremap.
// Update the page table etc in the syscall.
// The curthread there would take care of this.
vaddr_t alloc_upages(int npages) {

  vaddr_t va;

  // Offload the job of the magic function to the getppages function.

  va = getppages(npages, DIRTY);

  return va;

}

/*void page_free() {

  if ()
}*/

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
  //vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop, vbase, vtop;
  vaddr_t stackbase, stacktop, vbase, vtop;
  paddr_t paddr;
  int i, flag;
  uint32_t ehi, elo;
  struct addrspace *as;
  int spl;
  struct regionlistnode *rlnode;

  faultaddress &= PAGE_FRAME;

  DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

  switch (faulttype) {
      case VM_FAULT_READONLY:
    /* We always create pages read-write, so we can't get this */
    panic("dumbvm: got VM_FAULT_READONLY\n");
      case VM_FAULT_READ:
      case VM_FAULT_WRITE:
    break;
      default:
    return EINVAL;
  }

  as = curthread->t_addrspace;
  if (as == NULL) {
    /*
     * No address space set up. This is probably a kernel
     * fault early in boot. Return EFAULT so as to panic
     * instead of getting into an infinite faulting loop.
     */
    return EFAULT;
  }

  as = curthread->t_addrspace;
  if (as == NULL) {

     // No address space set up. This is probably a kernel
     // fault early in boot. Return EFAULT so as to panic
     // instead of getting into an infinite faulting loop.
    return EFAULT;
  }

  // Assert that the address space has been set up properly.
  KASSERT(as->stackpbase != 0);
  KASSERT((as->stackpbase & PAGE_FRAME) == as->stackpbase);
  stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
  stacktop = USERSTACK;
  if (faultaddress >= stackbase && faultaddress < stacktop) {
    paddr = (faultaddress - stackbase) + as->stackpbase;
  }
  else {

    rlnode = as->regionlisthead;
    flag = 0;
    while (rlnode != NULL) {

      KASSERT(rlnode->vbase != 0);
      KASSERT(rlnode->pbase != 0);
      KASSERT(rlnode->npages != 0);
      KASSERT((rlnode->vbase & PAGE_FRAME) == rlnode->vbase);
      KASSERT((rlnode->pbase & PAGE_FRAME) == rlnode->pbase);

      vbase = rlnode->vbase;
      vtop = rlnode->vbase + rlnode->npages * PAGE_SIZE;

      if (faultaddress >= vbase && faultaddress < vtop) {
        paddr = (faultaddress - vbase) + rlnode->pbase;
        flag = 1;
        break;
      }

      rlnode = rlnode->next;
    }

    if (flag == 0) {
      flag = 0;
      return EFAULT;
    }
  }

  // Assert that the address space has been set up properly. */
  /////////////////////
  /*KASSERT(as->as_vbase1 != 0);
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
  }*/
  /////////////////////////////////////////

  /* make sure it's page-aligned */
  KASSERT((paddr & PAGE_FRAME) == paddr);

  /* Disable interrupts on this CPU while frobbing the TLB. */
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
