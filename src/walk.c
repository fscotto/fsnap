#define _DEFAULT_SOURCE
#include "walk.h"
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *blacklist[] = {".", ".."};

char *strconcat(const char *src, int n, ...) {
  va_list ap;

  /* First pass: total length. strcat never reallocates, so the buffer has to
     be sized up front. */
  size_t len = strlen(src);
  va_start(ap, n);
  for (int i = 0; i < n; i++) {
    len += strlen(va_arg(ap, char *));
  }
  va_end(ap);

  char *path = malloc(len + 1);
  if (path == NULL)
    return NULL;

  size_t off = strlen(src);
  memcpy(path, src, off);
  va_start(ap, n);
  for (int i = 0; i < n; i++) {
    const char *s = va_arg(ap, char *);
    size_t l = strlen(s);
    memcpy(path + off, s, l);
    off += l;
  }
  va_end(ap);
  path[off] = '\0';

  return path;
}

int ignore_file(const char *name) {
  for (long unsigned int i = 0; i < sizeof(blacklist) / sizeof(char *); i++) {
    if (strcmp(name, blacklist[i]) == 0)
      return 1;
  }
  return 0;
}

static int handle_unknown(const char *path, operation op) {
  struct stat st;

  if (lstat(path, &st) == -1) {
    return -1;
  }

  if (S_ISDIR(st.st_mode)) {
    return walk(path, op);
  }

  op(path);
  return 0;
}

int walk(const char *directory, operation op) {
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
      if (handle_unknown(path, op) == -1) {
        return -1;
      }
      break;
    case DT_DIR:
      if (walk(path, op) == -1) {
        return -1;
      }
      break;
    default:
      op(path);
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
