// Including all these files to prevent stupid build errors.
// (Copied from thread.h). kern/fcntl.h hahaha #sorry
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
#include "../../user/include/errno.h"
#include <uio.h>

#include <file.h>


/*
 * File sys_calls are defined here.
 * Declarations are in kern/include/file.h
 */


/* Assign a file descriptor. 0, 1, 2 are not defined right now.
 * These are defined when they are used to avoid complications
 * when doing a fork() (even for init).
 */

int sys_open(const char *filename, int flags, int mode) {

  char *k_filename, *k_con;
  int fd, result;
  int errno;
  size_t length;

  fd = -1;

  // Check valid name. There isn't a specific error code
  // for having too long a name.
  if (filename == "" || strlen(filename) > NAME_MAX) {
    errno = EFAULT;
    return -1;
  }

  // Check valid flags. AND with 0_ACCMODE to extract them.
  flags = flags & O_ACCMODE;
  /*if (flags == 0 || flags == 1 || flags == 2) {
    errno = EINVAL;
    return -1;
  }*/

  // Filename is in userland.
  k_filename = kmalloc(sizeof(char) * strlen(filename));
  KASSERT(k_filename != NULL);
  copyinstr((const_userptr_t)filename, k_filename, PATH_MAX, &length);

  //lock_acquire(node->lock);

  if (filename == "con:" && flags == O_RDONLY)
    fd = 0;
  else if (filename == "con:" && flags == O_WRONLY && mode == 0664)
    fd = 1;
  else if (filename == "con:" && flags == O_WRONLY && mode == 0665) {
    fd = 2;
    mode = 0664;
  }
  else {
    fd = 3;
    while (&curthread->file_desctable[fd] != NULL && fd < OPEN_MAX)
      fd++;
  }

  // no open file table
  if (fd == OPEN_MAX)
    kprintf("Death by file table\n");

  // Check that the file was not allocated.
  if (curthread->file_desctable[fd] == NULL)
    curthread->file_desctable[fd] = kmalloc(sizeof(struct File));
  
  if (filename == "con:") {
    k_con = kmalloc(strlen("con:") * sizeof(char));
    strcpy(k_con, "con:");
    curthread->file_desctable[fd]->name = k_con;
  }
  else
    curthread->file_desctable[fd]->name = k_filename; 
   
  // VOP_OPEN(node->vn, flags)
  if((result = vfs_open(curthread->file_desctable[fd]->name, flags, mode, &curthread->file_desctable[fd]->vn)))
    kprintf ("Something went wrong opening the file %d, result %d", fd, result);

  // Do the file stuff.
  curthread->file_desctable[fd]->ref_count = 1;
  curthread->file_desctable[fd]->offset = 0;
  curthread->file_desctable[fd]->flags = flags;
 
  return result;
}

int sys_close(int fd) {

  struct File *temp_file;
  int errno;
  int temp_ref_count;

  if (fd < 0 && fd > OPEN_MAX) {
    errno = EBADF;
    return -1;
  }

  // File already closed.
  if (curthread->file_desctable[fd] == NULL) {
    errno = ENOFD;
    return -1;
  }

  temp_ref_count = curthread->file_desctable[fd]->ref_count;
  if (temp_ref_count == 1) {
    VOP_CLOSE(curthread->file_desctable[fd]->vn);
    temp_ref_count = 0;
    temp_file = curthread->file_desctable[fd];
    curthread->file_desctable[fd] = NULL;
    kfree(temp_file);
  }
  else {
    temp_ref_count--;
    curthread->file_desctable[fd]->ref_count = temp_ref_count;
  }

  return 0;
}

int sys_read(int fd, void *buf, size_t buflen) {

  // flags should be: O_RDONLY for stdin options should be 0664
  struct uio *read_uio;
  struct iovec *read_iovec;
  char *k_buf;
  int result;

  // Do the fd checks. The buf checks are handles by the UIO_READ. buflen, should I trust you?
  // if (buflen != sizeof(buf))
  //  print "yeah right"

  // Find out why this is not needed.
  read_uio = kmalloc(sizeof(struct uio));
  read_iovec = kmalloc(sizeof(struct iovec));

  k_buf = kmalloc(sizeof(buflen) * sizeof(char));

  uio_kinit(read_iovec, read_uio, k_buf, buflen, 0, UIO_READ);

  if (fd == 0) {
    if ((result = sys_open("con:", O_RDONLY, 0664)))
      kprintf("Something went wrong opening the console during a read, %d, result %d.\n", fd, result);
    //curthread->file_desctable[fd]->ref_count++;
  }

  VOP_READ(curthread->file_desctable[fd]->vn, read_uio);

  if (fd == 0 || fd == 1 || fd == 2)
    sys_close(fd);

  copyout(k_buf, buf, sizeof(buflen));

  if (fd == 0)
    sys_close(fd);

  return 0;
}

int sys_write(int fd, const void *buf, size_t nbytes) {

  struct uio *write_uio;
  struct iovec *write_iovec;
  char *k_buf;
  size_t length;
  int result, mode;

  // Do the bullet proofing later.

  k_buf = kmalloc(sizeof(nbytes) * sizeof(char));

  copyinstr((const_userptr_t)buf, k_buf, nbytes, &length);

  write_uio = kmalloc(sizeof(struct uio));

  write_iovec = kmalloc(sizeof(struct iovec));

  uio_kinit(write_iovec, write_uio, k_buf, nbytes, 0, UIO_WRITE);

  if (fd == 1)
    mode = 0664;
  else if (fd == 2)
    mode = 0665;

  if (fd == 1 || fd == 2) {

    if ((result = sys_open("con:", O_WRONLY, mode))) {
      kprintf("Something went wrong opening the console during a write %d, result %d\n", fd, result);
      return -1;
    }

    // Don't increment ref_count.
    curthread->file_desctable[fd]->offset = 0;
  }
 
  VOP_WRITE(curthread->file_desctable[fd]->vn, write_uio);

  if (fd == 1 || fd == 2)
    sys_close(fd);

  return 0;
}
