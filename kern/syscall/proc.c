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

// System processes.
static struct thread * process_table[PID_MAX];

// Zombie table. Double tap to be sure. Bad idea.
//static struct thread * zombie_table[PID_MAX];

pid_t get_next_pid(struct thread *new_thread) {

  pid_t pid;
  int errno;

  pid = PID_MIN;
  while (process_table[pid] != NULL)
    pid++;

  if (pid == PID_MAX) {
    errno = ENPROC;
    return -1;
  }

  process_table[pid] = new_thread;

  //kprintf("I got here in get_next_pid\n");
  return pid;
}

void free_this_pid(pid_t pid) {
  process_table[pid] = NULL;
}

struct thread * get_thread_by_pid(pid_t pid) {
  return process_table[pid];
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

pid_t sys_getpid(void) {

  return curthread->process->pid;
}


int sys_fork(struct trapframe *tf, pid_t *retval) {

  struct addrspace *new_addrspace;
  int result, errno, i;
  struct trapframe *new_tf;
  struct thread *child;

  //new_addrspace = kmalloc(sizeof(struct addrspace));
  if((result = as_copy(curthread->t_addrspace, &new_addrspace))) {
    errno = ENOMEM;
    return result;
  }

  new_tf = kmalloc(sizeof(struct trapframe));
  *new_tf = *tf;

  // figure out how to get the name.
  if((result = thread_fork("child", child_fork_entry, (struct trapframe *)new_tf, (unsigned long)new_addrspace, &child)))
    return result;

  i = 3;
  // Increase the reference count.
  while (curthread->file_desctable[i] != NULL) {
    curthread->file_desctable[i]->ref_count += 1;
    child->file_desctable[i] = curthread->file_desctable[i];
  }

  // Stupid, Me
  //*retval = get_next_pid(child);
  /*if (child->process->pid == child->process->ppid)
    *retval = get_next_pid(child);
  else
    *retval = child->process->pid;*/

  *retval = child->process->pid;

  return 0;
}

int sys_waitpid(pid_t pid, int *status, int options, int *retval) {

  int errno;
  struct thread *child;

  // WAITANY(-1) and WAITMYPGM(0) are not supported?

  if (options != 0) {
    errno = EINVAL;
    return -1;
  }

  // Waiting for yourself.
  if (pid == curthread->process->pid) {
    errno = ECHILD;
    return -1;
  }

  if (get_thread_by_pid(pid) == NULL) {
    errno = ESRCH;
    return -1;
  }


  if (child->process->ppid != curthread->process->pid) {
    errno = ECHILD;
    return -1;
  }

  child = get_thread_by_pid(pid);

  if (child->process->exited == 0) {

    // Check that we are not waiting on a parent
    if (curthread->process->ppid == child->process->pid) {
      errno = ECHILD;
      return -1;
    }

    P(child->process->exit);
  }

  *status = child->process->exitcode;

  *retval = pid;

  free_this_pid(pid);

  return 0;
}

void sys__exit(int exitcode) {

  // Condition where parent has exited before the child.

  curthread->process->exitcode = _MKWVAL(exitcode);

  curthread->process->exited = 1;

  V(curthread->process->exit);

  thread_exit();
}
