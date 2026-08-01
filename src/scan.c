#include "scan.h"
#include "utility.h"
#include "walk.h"
#include <stdio.h>
#include <string.h>

static int print_file(const char *file, void *context) {
  (void)context;
  printf("[%s]\n", file);
  return 0;
}

/* Scan recursively directory, printing all entry */
int scan(const char *directory) {
  if (directory == NULL || strlen(directory) == 0)
    return -1;

  const char *dir = resolve_path(directory);
  if (dir == NULL)
    return -1;
  return walk(dir, print_file, NULL);
}
