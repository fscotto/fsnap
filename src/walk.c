#define _GNU_SOURCE
#include "utility.h"
#include "walk.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *blacklist[] = {".", ".."};

static int ignore_file(const char *name) {
  for (long unsigned int i = 0; i < sizeof(blacklist) / sizeof(char *); i++) {
    if (strcmp(name, blacklist[i]) == 0)
      return 1;
  }
  return 0;
}

static int handle_unknown(const char *path, operation op, void *context) {
  struct stat st;

  if (lstat(path, &st) == -1) {
    return -1;
  }

  if (S_ISDIR(st.st_mode)) {
    if (op(path, context) == -1)
      return -1;
    return walk(path, op, context);
  }

  if (op(path, context) == -1)
    return -1;
  return 0;
}

int walk(const char *directory, operation op, void *context) {
  errno = 0;
  DIR *dirp = opendir(directory);
  if (dirp == NULL) {
    if (errno == EACCES) {
      fprintf(stderr, "access denied: %s\n", directory);
      return 0;
    }
    return -1;
  }

  int rc = -1;
  char *path = NULL;
  struct dirent *direntp = NULL;

  errno = 0;
  while ((direntp = readdir(dirp)) != NULL) {
    if (ignore_file(direntp->d_name))
      continue;

    path = strconcat(directory, 2, "/", direntp->d_name);
    if (path == NULL)
      goto out;

    switch (direntp->d_type) {
    case DT_UNKNOWN:
      // operating system or filesystem do not support dirent->d_type.
      // In this case use alternative algorithm for walk directories.
      if (handle_unknown(path, op, context) == -1)
        goto out;
      break;
    case DT_DIR:
      if (op(path, context) == -1)
        goto out;
      if (walk(path, op, context) == -1)
        goto out;
      break;
    default:
      if (op(path, context) == -1)
        goto out;
      break;
    }

    free(path);
    path = NULL;
    errno = 0;
  }

  if (errno == 0) {
    rc = 0;
  }

out:
  free(path);
  if (closedir(dirp) == -1)
    rc = -1;
  return rc;
}
