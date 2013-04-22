// Global thread struct to store.

#include <limits.h>
#include <types.h>
#include <mips/trapframe.h>

#ifndef _PROC_H
#define _PROC_H

struct thread* pid_array[PID_MAX];

void child_fork_entry(void *data1, unsigned long data2);

int get_thread_by_pid(pid_t pid, struct thread *thread);
pid_t get_next_pid(struct thread *new_thread);

pid_t sys_getpid();
int sys_fork(struct trapframe *tf, pid_t *retval);

#endif
