#define _XOPEN_SOURCE 500
#include "scan.h"
#include "walk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PATH_MAX
static long pathmax = PATH_MAX;
#else
static long pathmax = 1024;
#endif

int print_file(const char *file) {
  printf("[%s]\n", file);
  return 0;
}

/* Scan recursively directory, printing all entry */
int scan(const char *directory) {
  if (directory == NULL || strlen(directory) == 0)
    return -1;
  char path[pathmax];
  const char *dir = NULL;
  if ((dir = realpath(directory, path)) == NULL) {
    perror("realpath");
    return -1;
  }

  return walk(dir, print_file);
}
