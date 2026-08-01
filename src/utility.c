#define _XOPEN_SOURCE 500
#include "utility.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

char *resolve_path(const char *path) {
  char resolved[pathmax];
  char *dir = NULL;
  if ((dir = realpath(path, resolved)) == NULL) {
    perror("realpath");
    return NULL;
  }
  return dir;
}
