#define _POSIX_C_SOURCE 200809L
#include "diff.h"
#include "types.h"
#include "utility.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load_in_memory(const char *file, struct RecordObject ***records,
                          int *count) {
  int exit_failure = -1;
  FILE *stream = NULL;
  char *buf = NULL;

  if ((stream = fopen(file, "r")) == NULL)
    goto failure;

  *count = count_lines(file);
  if (*count == 0)
    goto success;
  else if (*count < 0)
    goto failure;

  *records = calloc((size_t)*count, sizeof(**records));
  if (*records == NULL) {
    errno = ENOMEM;
    goto failure;
  }

  int i = 0;
  size_t size = 0;
  while (getline(&buf, &size, stream) != -1 && i < *count) {
    struct RecordObject *objp = NewRecordObject();
    if (objp == NULL) {
      errno = ENOMEM;
      goto failure;
    }

    int ret = Unpack(objp, buf);
    if (ret == -1) {
      errno = EINVAL;
      Release(objp);
      goto failure;
    }

    (*records)[i] = objp;
    i++;
  }

  if (ferror(stream))
    goto failure;

success:
  if (stream != NULL)
    fclose(stream);
  if (buf != NULL)
    free(buf);

  return 0;

failure:
  if (stream != NULL)
    fclose(stream);
  if (buf != NULL)
    free(buf);

  return exit_failure;
}

static int dealloc(struct RecordObject **records, int size) {
  for (int j = 0; j < size; j++) {
    Release(records[j]);
    records[j] = NULL;
  }
  free(records);
  return 0;
}

static int order_by_path(const void *o1, const void *o2) {
  const struct RecordObject *r1 = *(const struct RecordObject *const *)o1;
  const struct RecordObject *r2 = *(const struct RecordObject *const *)o2;
  return strcmp(GetPath(r1), GetPath(r2));
}

int diff(const char *file1, const char *file2) {
  int exit_failure = -1;
  struct RecordObject **records1 = NULL;
  struct RecordObject **records2 = NULL;

  int len1 = 0;
  int ret1 = load_in_memory(file1, &records1, &len1);
  if (ret1 == -1) {
    goto failure;
  } else if (ret1 > 0) {
    exit_failure = ret1;
    goto failure;
  }

  int len2 = 0;
  int ret2 = load_in_memory(file2, &records2, &len2);
  if (ret2 == -1) {
    goto failure;
  } else if (ret2 > 0) {
    exit_failure = ret2;
    goto failure;
  }

  qsort(records1, (size_t)len1, sizeof(records1[0]), order_by_path);
  qsort(records2, (size_t)len2, sizeof(records2[0]), order_by_path);

  int m = 0, n = 0;
  while (m < len1 && n < len2) {
    struct RecordObject *r1 = records1[m];
    struct RecordObject *r2 = records2[n];

    char code = 0;
    const char *path = NULL;
    int c = strcmp(GetPath(r1), GetPath(r2));
    if (c < 0) {
      code = 'D';
      path = GetPath(r1);
      m++;
    } else if (c > 0) {
      code = 'A';
      path = GetPath(r2);
      n++;
    } else if (Compare(r1, r2) != 0) {
      switch (field) {
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
      path = GetPath(r1);
      m++;
      n++;
    } else {
      m++;
      n++;
    }

    if (code != 0) {
      fprintf(stdout, "%c\t%s\n", code, path);
    }
  }

  while (m < len1) {
    const struct RecordObject *r = records1[m];
    fprintf(stdout, "D\t%s\n", GetPath(r));
    m++;
  }

  while (n < len2) {
    const struct RecordObject *r = records2[n];
    fprintf(stdout, "A\t%s\n", GetPath(r));
    n++;
  }

  if (records1 != NULL) {
    dealloc(records1, len1);
    records1 = NULL;
  }
  if (records2 != NULL) {
    dealloc(records2, len2);
    records2 = NULL;
  }

  return 0;

failure:
  if (records1 != NULL) {
    dealloc(records1, len1);
    records1 = NULL;
  }
  if (records2 != NULL) {
    dealloc(records2, len2);
    records2 = NULL;
  }

  return exit_failure;
}
