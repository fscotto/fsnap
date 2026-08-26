#include "diff.h"
#include "types.h"
#include "utility.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Returns 0 on success, -1 on a system error, 128 on a malformed record. */
static int load_in_memory(const char *file, struct RecordObject ***records,
                          int *count) {
  int rc = -1;
  FILE *stream = NULL;
  char *buf = NULL;

  if ((stream = fopen(file, "r")) == NULL)
    goto cleanup;

  *count = count_lines(file);
  if (*count < 0)
    goto cleanup;

  /* An empty snapshot is a valid one: create() produces it for an empty
     directory and list() accepts it. It simply carries no records, so every
     record in the other snapshot is an addition or a deletion. calloc(0, ..)
     may legitimately return NULL, hence the early return. */
  if (*count == 0) {
    rc = 0;
    goto cleanup;
  }

  *records = calloc((size_t)*count, sizeof(**records));
  if (*records == NULL) {
    errno = ENOMEM;
    goto cleanup;
  }

  int i = 0;
  size_t size = 0;
  while (getline(&buf, &size, stream) != -1 && i < *count) {
    struct RecordObject *objp = RecordObjectNew();
    if (objp == NULL) {
      errno = ENOMEM;
      goto cleanup;
    }

    enum Fields err = NONE;
    errno = 0;
    int ret = RecordObjectUnpack(objp, buf, &err);
    if (ret == -1) {
      /* A failed allocation inside the parser is not a malformed record and
         must not be reported as one; errno tells them apart. */
      if (errno != EINVAL) {
        RecordObjectRelease(objp);
        goto cleanup;
      }

      /* Same diagnostic shape as list(): identify the snapshot, the line and
         the offending field. A malformed record is not a system error, so it
         gets its own status and main() leaves errno alone. */
      if (err != NONE)
        fprintf(stderr, "%s:%d: invalid %s\n", file, i + 1,
                RecordObjectFieldName(err));
      else
        fprintf(stderr, "%s:%d: unparsable line\n", file, i + 1);
      RecordObjectRelease(objp);
      rc = 128;
      goto cleanup;
    }

    (*records)[i] = objp;
    i++;
  }

  if (ferror(stream))
    goto cleanup;

  rc = 0;

cleanup:;
  const int saved_errno = errno;
  if (stream != NULL)
    fclose(stream);
  free(buf);
  errno = saved_errno;
  return rc;
}

static void dealloc(struct RecordObject **records, int size) {
  for (int j = 0; j < size; j++) {
    RecordObjectRelease(records[j]);
    records[j] = NULL;
  }
  free(records);
}

static int order_by_path(const void *o1, const void *o2) {
  const struct RecordObject *r1 = *(const struct RecordObject *const *)o1;
  const struct RecordObject *r2 = *(const struct RecordObject *const *)o2;
  return strcmp(RecordObjectPath(r1), RecordObjectPath(r2));
}

int diff(const char *file1, const char *file2) {
  int rc = -1;
  struct RecordObject **records1 = NULL;
  struct RecordObject **records2 = NULL;

  int len1 = 0;
  if ((rc = load_in_memory(file1, &records1, &len1)) != 0)
    goto cleanup;

  int len2 = 0;
  if ((rc = load_in_memory(file2, &records2, &len2)) != 0)
    goto cleanup;

  if (records1 != NULL)
    qsort(records1, (size_t)len1, sizeof(records1[0]), order_by_path);
  if (records2 != NULL)
    qsort(records2, (size_t)len2, sizeof(records2[0]), order_by_path);

  int m = 0, n = 0;
  while (m < len1 && n < len2) {
    struct RecordObject *r1 = records1[m];
    struct RecordObject *r2 = records2[n];

    char code = 0;
    const char *path = NULL;
    int c = strcmp(RecordObjectPath(r1), RecordObjectPath(r2));
    if (c < 0) {
      code = 'D';
      path = RecordObjectPath(r1);
      m++;
    } else if (c > 0) {
      code = 'A';
      path = RecordObjectPath(r2);
      n++;
    } else {
      enum Fields err = NONE;
      if (RecordObjectCompare(r1, r2, &err) != 0) {
        switch (err) {
        case TARGET:
          code = 'L';
          break;
        case PERMISSIONS:
          code = 'P';
          break;
        default:
          code = 'M';
          break;
        }
        path = RecordObjectPath(r1);
        m++;
        n++;
      } else {
        m++;
        n++;
      }
    }

    if (code != 0) {
      fprintf(stdout, "%c\t%s\n", code, path);
    }
  }

  while (m < len1) {
    const struct RecordObject *r = records1[m];
    fprintf(stdout, "D\t%s\n", RecordObjectPath(r));
    m++;
  }

  while (n < len2) {
    const struct RecordObject *r = records2[n];
    fprintf(stdout, "A\t%s\n", RecordObjectPath(r));
    n++;
  }

  /* stdout is block-buffered when redirected, so a write error can surface
     only here. */
  if (fflush(stdout) == EOF) {
    rc = -1;
    goto cleanup;
  }

  rc = 0;

cleanup:
  if (records1 != NULL)
    dealloc(records1, len1);
  if (records2 != NULL)
    dealloc(records2, len2);
  return rc;
}
