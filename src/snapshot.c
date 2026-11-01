#include "snapshot.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void snapshot_release(struct RecordObject **records, int count) {
  for (int i = 0; i < count; i++)
    RecordObjectRelease(records[i]);
  free(records);
}

int snapshot_load(const char *file, struct RecordObject ***out, int *out_count) {
  *out = NULL;
  *out_count = 0;
  FILE *stream = fopen(file, "r");
  if (stream == NULL)
    return -1;

  int rc = -1, count = 0;
  size_t capacity = 0, size = 0, line = 0;
  unsigned version = 0;
  char *buf = NULL;
  struct RecordObject **records = NULL;
  struct stat before, after;
  if (fstat(fileno(stream), &before) == -1)
    goto cleanup;

  ssize_t length;
  while ((length = getline(&buf, &size, stream)) != -1) {
    line++;
    if (memchr(buf, '\0', (size_t)length) != NULL) {
      fprintf(stderr, "%s:%zu: embedded NUL byte\n", file, line);
      rc = FSNAP_EPARSE;
      goto cleanup;
    }
    if (line == 1 && strncmp(buf, "FSNAP|", 6) == 0) {
      if (strcmp(buf, "FSNAP|1\n") != 0 && strcmp(buf, "FSNAP|1") != 0) {
        fprintf(stderr, "%s:1: unsupported snapshot version\n", file);
        rc = FSNAP_EPARSE;
        goto cleanup;
      }
      version = 1;
      continue;
    }

    if ((size_t)count == capacity) {
      size_t next = capacity == 0 ? 64 : capacity * 2;
      if (next > INT_MAX)
        next = INT_MAX;
      if (next <= capacity || next > SIZE_MAX / sizeof(*records)) {
        errno = EOVERFLOW;
        goto cleanup;
      }
      struct RecordObject **grown = realloc(records, next * sizeof(*records));
      if (grown == NULL)
        goto cleanup;
      records = grown;
      capacity = next;
    }
    struct RecordObject *record = RecordObjectNew();
    if (record == NULL)
      goto cleanup;
    enum Fields field = NONE;
    if (RecordObjectUnpack(record, buf, &field, version) == -1) {
      const int err = errno;
      RecordObjectRelease(record);
      if (err == EINVAL) {
        if (field != NONE)
          fprintf(stderr, "%s:%zu: invalid %s\n", file, line,
                  RecordObjectFieldName(field));
        else
          fprintf(stderr, "%s:%zu: unparsable line\n", file, line);
        rc = FSNAP_EPARSE;
      }
      errno = err;
      goto cleanup;
    }
    records[count++] = record;
  }
  if (ferror(stream) || !feof(stream))
    goto cleanup;

  if (fstat(fileno(stream), &after) == -1)
    goto cleanup;
  if (S_ISREG(before.st_mode) &&
      (before.st_size != after.st_size ||
       before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
       before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
       before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
       before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)) {
    fprintf(stderr, "%s: snapshot changed while reading\n", file);
    errno = EAGAIN;
    goto cleanup;
  }
  rc = 0;

cleanup:;
  const int saved_errno = errno;
  if (fclose(stream) != 0 && rc == 0) {
    rc = -1;
  } else {
    errno = saved_errno;
  }
  free(buf);
  if (rc == 0) {
    *out = records;
    *out_count = count;
  } else {
    const int err = errno;
    snapshot_release(records, count);
    errno = err;
  }
  return rc;
}
