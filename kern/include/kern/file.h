/*
 * file.h contains the declarations that are needed
 * for the File system calls (defined in kern/syscalls/file.c)
 */

#ifndef _FILE_H_
#define _FILE_H_

#include <limits.h>

struct File {
  char name[NAME_MAX];
  int flags;
  off_t offset;
  int ref_count;
  struct lock* lock;
  struct vnode* vn;
};

int sys_open(const char *filename, int flags, int mode);
//int sys_close(int fd);
//int write(int fd, const void *buf, size_t nbytes>);

#endif
