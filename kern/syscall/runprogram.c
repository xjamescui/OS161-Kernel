/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *  The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>

#include <copyinout.h>

#define ARGSIZE 20

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname, char **args)
{
  struct vnode *v;
  int argc, result, temp, i;
  char *kargs, *name, *userptrs[10] , *ptr;
  vaddr_t entrypoint, userstk;

  /* Open the file. */
  name = kstrdup(progname);
  result = vfs_open(progname, O_RDONLY, 0, &v);
  if (result) {
    return result;
  }

  /* We should be a new thread. */
  KASSERT(curthread->t_addrspace == NULL);

  /* Create a new address space. */
  struct addrspace *tempaddr;
  tempaddr = curthread->t_addrspace;
  curthread->t_addrspace = as_create();
  if (curthread->t_addrspace==NULL) {
    vfs_close(v);
    return ENOMEM;
  }
  /* Activate it. */
  as_activate(curthread->t_addrspace);
  as_destroy(tempaddr);
  //kfree(tempaddr);


  /* Load the executable. */
  result = load_elf(v, &entrypoint);
  if (result) {
    /* thread_exit destroys curthread->t_addrspace */
    vfs_close(v);
    return result;
  }

  /* Done with the file now. */
  vfs_close(v);

  /* Define the user stack in the address space */
  result = as_define_stack(curthread->t_addrspace, &userstk);
  if (result) {
    /* thread_exit destroys curthread->t_addrspace */
    return result;
  }


  argc = 0;
  while (1) {

    ptr = (char *)kmalloc(sizeof(char));

    if (args[argc] == NULL || args[argc] == "\b") {
      userstk -= 4 * sizeof(char);
      break;
    }
    ptr = kstrdup(args[argc]);

    kargs = (char *)kmalloc(sizeof(char) * ARGSIZE);
    kargs = kstrdup(ptr);
    kfree(ptr);

    temp = strlen(kargs) + 1;
    temp += (4 - (temp % 4));
    for (i = strlen(kargs) + 1; i < temp; i++)
      kargs[i] = '\0';

    // Move the stakptr;
    userstk -= temp;

    //copyout the arg to stack.
    if((result = copyout((const void *)kargs , (userptr_t)userstk, temp))) {
      return result;
    }

    kfree(kargs);

    userptrs[argc] = (char *)userstk;

    argc++;
  }

  // Pack the user pointers into the user stack.
  for (i = argc - 1; i >= 0; i--) {

    userstk -= sizeof(char *);

    //if((result = copyout((const void *)userptrs[i] , (userptr_t)userstk, sizeof(char *)))) { :| :/
    if((result = copyout((const void *)(userptrs + i) , (userptr_t)userstk, sizeof(char *)))) {
      return result;
    }
  }


  // What?! I'm Agent Smith?!
  enter_new_process(argc, (userptr_t)userstk, userstk, entrypoint);

  /* enter_new_process does not return. */
  panic("enter_new_process returned\n");
  return EINVAL;
}
