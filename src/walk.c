#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include "walk.h"
#include "utility.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *blacklist[] = {".", ".."};

static int ignore_file(const char *name) {
  for (long unsigned int i = 0; i < sizeof(blacklist) / sizeof(char *); i++) {
    if (strcmp(name, blacklist[i]) == 0 || strcasestr(name, ".fsnap"))
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
    return walk(path, op, context);
  }

  op(path, context);
  return 0;
}

int walk(const char *directory, operation op, void *context) {
  DIR *dirp = opendir(directory);
  if (dirp == NULL) {
    return -1;
  }

  errno = 0;
  char *curr = NULL;
  struct dirent *direntp = NULL;
  while ((direntp = readdir(dirp)) != NULL) {
    curr = direntp->d_name;
    if (ignore_file(curr))
      continue;

    char *path = strconcat(directory, 2, "/", curr);
    if (path == NULL)
      return -1;

    switch (direntp->d_type) {
    case DT_UNKNOWN:
      // operating system or filesystem do not support dirent->d_type.
      // In this case use alternative algorithm for walk directories.
      if (handle_unknown(path, op, context) == -1) {
        return -1;
      }
      break;
    case DT_DIR:
      if (walk(path, op, context) == -1) {
        return -1;
      }
      break;
    default:
      op(path, context);
      break;
    }

    if (path != NULL)
      free(path);
  }

  if (errno != 0) {
    return -1;
  }
  return closedir(dirp);
}
