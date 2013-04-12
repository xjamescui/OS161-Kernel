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
#include <../../user/include/errno.h>
#include <uio.h>

#include <file.h>

/*
 * File sys_calls are defined here.
 */

/* Assign a file descriptor. 0, 1, 2 are not defined right now.
 * These are defined when they are used to avoid complications
 * when doing a fork() (even for init).
 */
// retval is going to be the fd. The return value is going to be error code.
int sys_open(const char *filename, int flags, int mode, int32_t *retval) {

  char *k_filename, *k_con;
  int fd, result, temp;
  int errno;
  size_t length;

  // Check if it works without this.
  fd = -1;

  // Check valid name.
  if (filename == "" || strlen(filename) > NAME_MAX) {
    errno = EFAULT;
    return -1;
  }

  // Check valid flags. AND with 0_ACCMODE to extract them.
  temp = flags & O_ACCMODE;
  if (!(temp == O_RDONLY || temp == O_WRONLY || temp == O_RDWR)) {
    errno = EINVAL;
    return -1;
  }

  // Filename is in userland.
  if (filename != "con:") {
    k_filename = kmalloc(sizeof(char) * strlen(filename));
    if (k_filename == NULL) {
      errno = ENOMEM;
      return -1;
    }

    copyinstr((const_userptr_t)filename, k_filename, PATH_MAX, &length);
  }

  if (filename == "con:" && temp == O_RDONLY)
    fd = 0;
  else if (filename == "con:" && temp == O_WRONLY && mode == 0664)
    fd = 1;
  else if (filename == "con:" && temp == O_WRONLY && mode == 0665) {
    fd = 2;
    mode = 0664;
  }
  else {
    get_new_name:fd = 3;
    while (curthread->file_desctable[fd] != NULL && fd < OPEN_MAX)
      fd++;
  }

  // Death by file table.
  if (fd == OPEN_MAX) {
    errno = EMFILE;
    kfree(k_filename);
    return -1;
  }

  // Check that the file was not allocated.
  if (curthread->file_desctable[fd] == NULL) {
    curthread->file_desctable[fd] = kmalloc(sizeof(struct File));
    if (filename == "con:") {
      k_con = kmalloc(strlen("con:") * sizeof(char));
      strcpy(k_con, "con:");
      curthread->file_desctable[fd]->name = k_con;
    }
    else
      curthread->file_desctable[fd]->name = k_filename;
  }
  else { // What you have done here is correct, not for the console.
    // Check that the threads match and have a goto otherwise.
    // If the names are the same, they probably are the same
    // user thread. Hopefully there aren't cooler race conditions.
    if (filename != "con:") {
      if (curthread->file_desctable[fd]->name != k_filename)
        goto get_new_name;
    }
  }

  // VOP_OPEN(node->vn, flags)
  if((result = vfs_open(curthread->file_desctable[fd]->name, flags, mode, &curthread->file_desctable[fd]->vn))) {
    errno = EIO;
    kfree(k_filename);
    kfree(k_con);
    kfree(curthread->file_desctable[fd]);
    curthread->file_desctable[fd] = NULL;
    kprintf ("Something went wrong opening the file %d, result %d", fd, result);
    return -1;
  }

  // Do the file stuff.
  curthread->file_desctable[fd]->ref_count = 1;
  curthread->file_desctable[fd]->offset = 0;
  curthread->file_desctable[fd]->flags = flags;

  *retval = fd;

  return 0;
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

int sys_read(int fd, void *buf, size_t buflen, int32_t *retval) {

  // flags should be: O_RDONLY for stdin options should be 0664
  struct uio *read_uio;
  struct iovec *read_iovec;
  char *k_buf;
  int errno;

  // The buf checks are handles by the UIO_READ.
  // if (buflen != sizeof(buf))
  //  print "yeah right"

  if (fd < 0 && fd > OPEN_MAX) {
    errno = EBADF;
    return -1;
  }

  // Read from the console. Open everytime.
  if (fd == 0)
    sys_open("con:", O_RDONLY, 0664, &fd);

  // Check if the normal file was opened before
  if (curthread->file_desctable[fd] == NULL) {
    errno = EBADF;
    return -1;
  }

  read_uio = kmalloc(sizeof(struct uio));
  read_iovec = kmalloc(sizeof(struct iovec));

  k_buf = kmalloc(sizeof(buflen) * sizeof(char));

  uio_kinit(read_iovec, read_uio, k_buf, buflen, 0, UIO_READ);

  *retval = VOP_READ(curthread->file_desctable[fd]->vn, read_uio);

  curthread->file_desctable[fd]->offset += *retval;

  if (fd == 0)
    sys_close(fd);

  copyout(k_buf, buf, sizeof(buflen));

  kfree(k_buf);
  kfree(read_uio);
  kfree(read_iovec);

  return 0;
}

int sys_write(int fd, const void *buf, size_t nbytes, int32_t *retval) {

  struct uio *write_uio;
  struct iovec *write_iovec;
  char *k_buf;
  size_t length;
  int errno;

  if (fd < 0 && fd > OPEN_MAX) {
    errno = EBADF;
    return -1;
  }

  if (fd == 1) {
    sys_open("con:", O_WRONLY, 0664, &fd);
  }
  else if (fd == 2) {
    sys_open("con:", O_WRONLY, 0665, &fd);
  }

  if (curthread->file_desctable[fd] == NULL) {
    errno = EBADF;
    return -1;
  }

  k_buf = kmalloc(sizeof(nbytes) * sizeof(char));

  copyinstr((const_userptr_t)buf, k_buf, nbytes, &length);

  write_uio = kmalloc(sizeof(struct uio));

  write_iovec = kmalloc(sizeof(struct iovec));

  uio_kinit(write_iovec, write_uio, k_buf, nbytes, 0, UIO_WRITE);

  *retval = VOP_WRITE(curthread->file_desctable[fd]->vn, write_uio);

  curthread->file_desctable[fd]->offset += *retval;

  if (fd == 1 || fd == 2)
    sys_close(fd);

  kfree(k_buf);
  kfree(write_uio);
  kfree(write_iovec);

  return 0;
}

int sys_dup2(int oldfd, int newfd) {

  int errno;

  if (oldfd < 0 || newfd < 0) {
    errno = EBADF;
    return -1;
  }

  if (oldfd == newfd)
    return 0;

  // Check that the newfd is null else close it.
  if (curthread->file_desctable[newfd] != NULL) {
    sys_close(newfd);
    curthread->file_desctable[newfd] = NULL;
  }

  curthread->file_desctable[newfd] = curthread->file_desctable[oldfd];
  curthread->file_desctable[newfd]->ref_count++;

  return 0;
}

int sys_chdir(const char *pathname) {

  char *k_pathname;
  int result;
  size_t size;

  result = 0;

  size = sizeof(pathname);

  k_pathname = kmalloc(size * sizeof(char));

  copyinstr((const_userptr_t)pathname, k_pathname, size, &size);

  if ((result = vfs_chdir(k_pathname))) {
    kfree(k_pathname);
    return -1;
  }

  kfree(k_pathname);

  return 0;
}

// You need VOP_STAT for lseek. VOP_TRYSEEK
/*off_t sys_lseek(int fd, off_t pos, int whence) {

}*/

// vfs_getcwd
/*int sys__getcwd(char *buf, size_t buflen) {
}*/
