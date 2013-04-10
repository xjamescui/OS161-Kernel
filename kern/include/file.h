/*
 * file.h contains the declarations that are needed
 * for the File system calls (defined in kern/syscalls/file.c)
 */

#ifndef _FILE_H_
#define _FILE_H_

struct File {
  char *name;
  int flags;
  off_t offset;
  int ref_count;
  // We only care about the particular entry.
  struct lock* lock;
  struct vnode* vn;
};

int sys_open(const char *filename, int flags, int mode);
int sys_close(int fd);
int sys_read(int fd, void *buf, size_t buflen);
int sys_write(int fd, const void *buf, size_t nbytes);

#endif
