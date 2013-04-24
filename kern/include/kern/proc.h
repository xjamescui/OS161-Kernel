// Global thread struct to store.

#include <limits.h>
#include <types.h>
#include <mips/trapframe.h>
#include <synch.h>

#ifndef _PROC_H
#define _PROC_H

// Previously on It Does Not Work,
//extern struct thread * pid_array[PID_MAX];

struct Proc {

  pid_t ppid;
  pid_t pid;
  struct semaphore *exit;
  bool exited;
  int exitcode;
};


void child_fork_entry(void *data1, unsigned long data2);

struct thread * get_thread_by_pid(pid_t pid);
pid_t get_next_pid(struct thread *new_thread);
void free_this_pid(pid_t pid);

pid_t sys_getpid(void);
int sys_fork(struct trapframe *tf, pid_t *retval);
//int sys_waitpid(pit_t pid, int *status, int options, int *retval); Wow. This was fucking frustrating.
int sys_waitpid(pid_t pid, int *status, int options, int *retval);
void sys__exit(int exitcode);

#endif
