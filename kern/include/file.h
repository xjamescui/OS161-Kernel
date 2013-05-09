/*
 * file.h contains the declarations that are needed
 * for the File system calls (defined in kern/syscalls/file.c)
 */

#ifndef _FILE_H_
#define _FILE_H_

#include <types.h>

struct File {
  char *name;
  int flags;
  off_t offset;
  int ref_count;
  // We only care about the particular entry.
  struct lock *lock;
  struct vnode* vn;
};

int sys_open(const char *filename, int flags, int mode, int32_t *retval);
int sys_close(int fd);
int sys_read(int fd, void *buf, size_t buflen, int32_t *retval);
int sys_write(int fd, const void *buf, size_t nbytes, int32_t *retval);
int sys_dup2(int oldfd, int newfd);
int sys_chdir(const_userptr_t *pathname);
int sys__getcwd(char *buf, size_t buflen);
off_t sys_lseek(int fd, off_t pos, int whence, off_t *retval);

#endif
