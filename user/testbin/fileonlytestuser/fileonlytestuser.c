#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

int
main(int argc, char **argv)
{

  (void) argc;
  (void) argv;
  int fh, len;
  off_t pos, target;

  char writebuf[13], readbuf[13];

  strcpy(writebuf, "abcdefghijkl");

  const char * filename = "fileonlytest.dat";

  printf("Opening %s\n", filename);

  fh = open(filename, O_RDWR|O_CREAT|O_TRUNC);
  if (fh < 0) {
    err(1, "create failed");
  }

  len = write(fh, writebuf, sizeof(writebuf));
  if (len != sizeof(writebuf)) {
    err(1, "write failed");
  }

  target = 0;
  pos = lseek(fh, target, SEEK_SET);
  if (pos != target) {
    err(1, "lseek failed: %llu != %llu", pos, target);
  }

  printf("Verifying write.\n");

  len = read(fh, readbuf, sizeof(readbuf));
  if (len != sizeof(readbuf)) {
    err(1, "read failed, len:%d", len);
  }

  printf("Closing %s\n", filename);
  close(fh);

  /*pos = lseek(fh, (off_t) 0, SEEK_SET);
  if (pos == 0) {
    err(1, "seek after close succeeded");
  }

  // 23 Mar 2012 : GWA : FIXME : Spin until exit() works.*/

  printf("Spinning in case exit() doesn't work.\n");
  while (1) {};

  return 0;
}
