#include "diff.h"
#include "types.h"
#include "snapshot.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int order_by_path(const void *o1, const void *o2) {
  const struct RecordObject *r1 = *(const struct RecordObject *const *)o1;
  const struct RecordObject *r2 = *(const struct RecordObject *const *)o2;
  return strcmp(RecordObjectPath(r1), RecordObjectPath(r2));
}

int diff(const char *file1, const char *file2) {
  int rc = -1;
  struct RecordObject **records1 = NULL;
  struct RecordObject **records2 = NULL;

  int len1 = 0, len2 = 0;
  if ((rc = snapshot_load(file1, &records1, &len1)) != 0)
    goto cleanup;

  if ((rc = snapshot_load(file2, &records2, &len2)) != 0)
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

cleanup:;
  const int saved_errno = errno;
  if (records1 != NULL)
    snapshot_release(records1, len1);
  if (records2 != NULL)
    snapshot_release(records2, len2);
  errno = saved_errno;
  return rc;
}
