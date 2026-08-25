#define _XOPEN_SOURCE 700
#include "utility.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  char *dir = NULL;
  if ((dir = realpath(path, NULL)) == NULL) {
    return NULL;
  }
  return dir;
}

int copy(const char *src, const char *dst) {
  FILE *file1 = fopen(src, "r");
  if (file1 == NULL) {
    return -1;
  }

  FILE *file2 = fopen(dst, "w+");
  if (file2 == NULL) {
    fclose(file1);
    return -1;
  }

  char buffer[8192]; // Buffer di 8KB
  size_t bytes;

  while ((bytes = fread(buffer, 1, sizeof(buffer), file1)) > 0) {
    if (fwrite(buffer, 1, bytes, file2) != bytes) {
      fclose(file1);
      fclose(file2);
      return -1;
    }
  }

  if (ferror(file1)) {
    fclose(file1);
    fclose(file2);
    return -1;
  }

  int rc = 0;
  if (fclose(file1) != 0)
    rc = -1;
  if (fclose(file2) != 0)
    rc = -1;

  return rc;
}

int count_lines(const char *file) {
  FILE *f = fopen(file, "r");
  if (f == NULL) {
    return -1;
  }

  size_t size = 0;
  int count = 0;
  char *buf = NULL;
  while (getline(&buf, &size, f) != -1)
    count++;

  if (ferror(f)) {
    /* Capture before the cleanup: free and fclose may both touch errno. */
    const int saved_errno = errno;
    free(buf);
    fclose(f);
    errno = saved_errno;
    return -1;
  }

  free(buf);
  if (fclose(f) != 0) {
    return -1;
  }
  return count;
}
