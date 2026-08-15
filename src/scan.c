#define _DEFAULT_SOURCE
#include "scan.h"
#include "utility.h"
#include "walk.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int print_file(const char *file, void *context) {
  (void)context;
  return printf("[%s]\n", file) < 0 ? -1 : 0;
}

/* Scan recursively directory, printing all entry */
int scan(const char *directory) {
  if (directory == NULL || strlen(directory) == 0) {
    errno = EINVAL;
    return -1;
  }

  char *dir = resolve_path(directory);
  if (dir == NULL)
    return -1;

  if (walk(dir, print_file, NULL) == -1) {
    const int saved_errno = errno;
    free(dir);
    errno = saved_errno;
    return -1;
  }
  free(dir);

  /* stdout is block-buffered when redirected, so a write error can surface
     only here. */
  if (fflush(stdout) == EOF)
    return -1;
  return 0;
}
