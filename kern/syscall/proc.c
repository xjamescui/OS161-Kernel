#include <proc.h>

#include <kern/types.h>
#include <limits.h>
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <array.h>
#include <cpu.h>
#include <spl.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <threadlist.h>
#include <threadprivate.h>
#include <current.h>
#include <synch.h>
#include <addrspace.h>
#include <mainbus.h>
#include <vnode.h>
#include <vfs.h>
#include <copyinout.h>
#include <kern/fcntl.h>
#include <../../user/include/errno.h>
#include <uio.h>
#include <synch.h>
#include <kern/wait.h>
#include <syscall.h>

#define ARGSIZE 20

// System processes.
static struct Proc * process_table[PID_MAX];

// Zombie table. Double tap to be sure. Bad idea.
//static struct thread * zombie_table[PID_MAX];

int assign_pid(struct thread *new_thread) {

  pid_t pid;
  int errno;
  struct Proc *entry;

  pid = PID_MIN;
  while (process_table[pid] != NULL)
    pid++;

  if (pid == PID_MAX) {
    errno = ENPROC;
    return -1;
  }

  entry = (struct Proc *)kmalloc(sizeof(struct Proc));

  new_thread->pid = pid;

  entry->ppid = new_thread->ppid;
  entry->pid = pid;
  entry->exited = 0;
  entry->exitcode = 0;
  entry->exit = sem_create("Child Sem", 0);
  entry->self = new_thread;

  process_table[pid] = entry;

  return pid;
}

void free_this_pid(pid_t pid) {

  struct Proc *proc;
  int a;

  a = splhigh();

  proc = get_process_by_pid(pid);

  if (proc != NULL) {
    sem_destroy(proc->exit);
    kfree(process_table[pid]);
    process_table[pid] = NULL;
  }

  splx(a);
}

struct Proc * get_process_by_pid(pid_t pid) {
    return process_table[pid];
}

struct thread * get_thread_by_pid(pid_t pid) {
  return process_table[pid]->self;
}

void child_fork_entry(void *data1, unsigned long data2) {

  struct addrspace* addrspace;
  struct trapframe tf, *tf_ptr;

  tf_ptr = (struct trapframe *)data1;

  // Indicate success.
  tf_ptr->tf_a3 = 0;
  tf_ptr->tf_v0 = 0;

  // goto next instruction after fork
  tf_ptr->tf_epc += 4;

  // Copy addrspace to stack and activate.
  addrspace = ((struct addrspace *)data2);
  curthread->t_addrspace = addrspace;
  // as_copy(&addrspace, &curthread->t_addrspace);
  as_activate(curthread->t_addrspace);

  // Copy trapfram and enter usermode.
  tf = *tf_ptr;
  mips_usermode(&tf);
}

// Process Sys Calls.

pid_t sys_getpid() {
  return curthread->pid;
}

int sys_fork(struct trapframe *tf, pid_t *retval) {

  struct addrspace *new_addrspace;
  int result, errno, i, a;
  struct trapframe *new_tf;
  struct thread *child;

  a = splhigh();

  //new_addrspace = kmalloc(sizeof(struct addrspace));
  if((result = as_copy(curthread->t_addrspace, &new_addrspace))) {
    errno = ENOMEM;
    splx(a);
    return result;
  }

  new_tf = kmalloc(sizeof(struct trapframe));
  *new_tf = *tf;

  splx(a);
  if((result = thread_fork("child", child_fork_entry, (struct trapframe *)new_tf, (unsigned long)new_addrspace, &child))) {
    return result;
  }
  a = splhigh();

  i = 3;
  // Increase the reference count.
  while (curthread->file_desctable[i] != NULL) {
    curthread->file_desctable[i]->ref_count += 1;
    child->file_desctable[i] = curthread->file_desctable[i];
  }

  child->ppid = curthread->pid;

  assign_pid(child);

  *retval = child->pid;

  splx(a);

  return 0;
}

int sys_waitpid(pid_t pid, int *status, int options, int *retval) {

  int errno, a;
  struct Proc *childp;

  a = splhigh();

  if (options != 0) {
    errno = EINVAL;
    splx(a);
    return -1;
  }

  // Waiting for yourself.
  if (pid == curthread->pid) {
    errno = ECHILD;
    splx(a);
    return -1;
  }

  childp = get_process_by_pid(pid);
  if (childp == NULL) {
    errno = ESRCH;
    splx(a);
    return -1;
  }

  // Check that we are not waiting on a parent
  if (childp->pid == curthread->ppid) {
    errno = ECHILD;
    splx(a);
    return -1;
  }

  // Check that the parent is waiting on the child
  if (childp->ppid != curthread->pid) {
    errno = ECHILD;
    splx(a);
    return -1;
  }

  if (childp->exited == 0) {
      P(childp->exit);
  }

  //*status = childp->exitcode; Gaaaaah.
  copyout(&childp->exitcode, (userptr_t)status, sizeof(int));

  *retval = pid;

  free_this_pid(pid);

  splx(a);

  return 0;
}

void sys__exit(int exitcode) {

  struct Proc *childp, *parentp;
  int a;

  a = splhigh();

  if (curthread->ppid >= 2) {

    parentp = get_process_by_pid(curthread->ppid);

    if (parentp == NULL) {
      free_this_pid(curthread->pid);
    }
    else {

      if (parentp->exited != 1) {

        childp = get_process_by_pid(curthread->pid);

        childp->exitcode = _MKWVAL(exitcode);

        childp->exited = 1;

        V(childp->exit);
      }
      else {
        free_this_pid(curthread->pid);
      }
    }
  }

  splx(a);

  thread_exit();
}

//int execv(const char *program, char **args);
int sys_execv(const char *program, char **args) {

  // #define ARGSIZE 20

  int argc, result, temp, i, errno;
  size_t actual;
  char *ptr, *name, *userptrs[10], *kkargs[10];
  struct vnode *vn;
  vaddr_t entrypoint, userstk;

  // There wan't a problem with this in file name copy
  // and I therefore suspect there isn't one here.
  // what happens to the case where the first argument is
  // the program name?

  // Check valid program.
  name = kmalloc(100 * sizeof(char));
  if ((result = copyinstr((const_userptr_t)program, name, 100, &actual))) {
    errno = ENOENT;
    return result;
  }

  argc = 0;
  while (1) {
    ptr = (char *)kmalloc(sizeof(char *));
    if((result = copyin((const_userptr_t)(args + argc), (void *)ptr, sizeof(char *)))) {
      errno = EFAULT;
      return result;
    }

    if (ptr == NULL || *ptr == '\0') {
      break;
    }

    kfree(ptr);

    kkargs[argc] = kmalloc(sizeof(char) * 100);
    copyinstr((const_userptr_t)args[argc], kkargs[argc], 100, &actual);
    argc++;
  }

  // Jackass. Code in the present.
  //kkargs[argc + 1] = NULL;
  kkargs[argc] = NULL;

  // Get the program into memory.
  //name = kstrdup(program);
  if ((result = vfs_open(name, O_RDONLY, 0, &vn))) {
    return result;
  }

  // Prepare addrspace.
  //KASSERT(curthread->t_addrspace == NULL);
  struct addrspace *tempaddr;
  tempaddr = curthread->t_addrspace;
  if((curthread->t_addrspace = as_create()) == NULL) {
    vfs_close(vn);
    errno = ENOMEM;
    return -1;
  }
  as_activate(curthread->t_addrspace);
  as_destroy(tempaddr);
  //kfree(tempaddr);


  // Gandalf The White.
  if((result = load_elf(vn, &entrypoint))) {
    vfs_close(vn);
    errno = ENOEXEC;
    return -1;
  }

  vfs_close(vn);

  // Setup user stack.
  if ((result = as_define_stack(curthread->t_addrspace, &userstk))) {
    return result;
  }

  // Get the args in and pad them. Again, jackass, code in
  // the present.
  argc = 0;
  while (1) {

    if (kkargs[argc] == NULL) {
      userstk -= 4 * sizeof(char);
      break;
    }

    // Pad.
    temp = strlen(kkargs[argc]) + 1;
    temp += (4 - (temp % 4));
    for (i = strlen(kkargs[argc]) + 1; i < temp; i++)
      kkargs[argc][i] = '\0';

    // Move the stakptr;
    userstk -= temp;

    //copyout the arg to stack.
    if((result = copyout((const void *)kkargs[argc], (userptr_t)userstk, temp))) {
      return result;
    }
    kfree(kkargs[argc]);

    userptrs[argc] = (char *)userstk;

    argc++;
  }

  // Pack the user pointers into the user stack.
  for (i = argc - 1; i >= 0; i--) {

    userstk -= sizeof(char *);

    if((result = copyout((const void *)(userptrs + i) , (userptr_t)userstk, sizeof(char *)))) {
      return result;
    }
  }

  // What?! I'm Agent Smith?!
  enter_new_process(argc, (userptr_t)userstk, userstk, entrypoint);

  panic("enter_new_process returned. Fusion failed.\n");
  return EINVAL;
}
