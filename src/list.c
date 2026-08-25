#define _POSIX_C_SOURCE 200809L
#include "list.h"
#include "types.h"
#include "utility.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int list(const char *snapshot) {
  FILE *snapshot_file = fopen(snapshot, "r");
  if (snapshot_file == NULL) {
    return -1;
  }

  int rc = 0;
  struct RecordObject **records = NULL;
  char *buf = NULL;
  const int arr_size = count_lines(snapshot);
  if (arr_size == 0) {
    goto out;
  } else if (arr_size < 0) {
    rc = -1;
    goto out;
  }

  int i = 0;
  size_t size = 0;
  records = calloc((size_t)arr_size, sizeof(*records));
  if (records == NULL) {
    rc = -1;
    goto out;
  }
  while (getline(&buf, &size, snapshot_file) != -1 && i < arr_size) {
    struct RecordObject *objp = NewRecordObject();
    if (objp == NULL) {
      rc = -1;
      goto out;
    }
    int ret = RecordObjectUnpack(objp, buf);
    if (field != NONE) {
      fprintf(stderr, "%s:%d: invalid %s\n", snapshot, i + 1, RecordObjectFieldName(field));
      rc = 128;
      RecordObjectRelease(objp);
      break;
    }

    if (ret == -1) {
      fprintf(stderr, "%s:%d: unparsable line\n", snapshot, i + 1);
      RecordObjectRelease(objp);
      rc = 128;
      break;
    }

    records[i] = objp;
    i++;
  }

  if (ferror(snapshot_file)) {
    rc = -1;
    goto out;
  }

out:;
  /* An error that happened before the cleanup owns errno; one raised by the
     cleanup itself owns it only when nothing had failed yet. */
  const int saved_errno = errno;
  int cleanup_errno = 0;

  if (snapshot_file != NULL && fclose(snapshot_file) != 0)
    cleanup_errno = errno;

  free(buf);
  if (records != NULL) {
    for (int j = 0; j < arr_size; j++) {
      RecordObjectRelease(records[j]);
      records[j] = NULL;
    }
    free(records);
    records = NULL;
  }

  if (rc == 0 && cleanup_errno != 0) {
    errno = cleanup_errno;
    return -1;
  }
  if (rc == -1)
    errno = saved_errno;
  return rc;
}
