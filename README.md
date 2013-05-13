OS161-Kernel
============

OS161 Kernel for CSE521

Supports synchronization, process system call, file system calls and virtual memory management.
Has bad swapping code and leaks a bit of memory under stress conditions. Use with more than 5M of RAM. Less than that
and swapping fails dramatically and the kernel dies.

To test the thing, you need the toolchain - http://www.eecs.harvard.edu/~dholland/os161/resources/setup.html
