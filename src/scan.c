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
  printf("[%s]\n", file);
  return 0;
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
    free(dir);
    return -1;
  }
  free(dir);
  return 0;
}
