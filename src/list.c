#include "list.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#ifdef LINE_MAX
#define LINEMAX LINE_MAX
#else
#define LINEMAX 2048
#endif

#define NFIELDS 7

enum Fields {
  FILE_TYPE,
  PERMISSIONS,
  UID,
  GID,
  SIZE,
  TIME,
  PATH,
  NONE,
};

static const char *field_names[] = {"file type", "permissions", "uid",  "gid",
                                    "size",      "time",        "path", "none"};

static enum Fields field = NONE;

static const char *field_name(enum Fields f) {
  return (f <= NONE) ? field_names[f] : "unknown";
}

static int split_record(char *line, char *fields[NFIELDS], char delim) {
  char *p = line;

  line[strcspn(line, "\n")] = '\0';

  for (size_t n = 0; n < NFIELDS - 1; n++) {
    fields[n] = p;

    char *sep = strchr(p, delim);
    if (sep == NULL)
      return -1;

    *sep = '\0';
    p = sep + 1;
  }

  fields[NFIELDS - 1] = p;

  if (strchr(fields[NFIELDS - 1], delim) != NULL)
    return -1;

  return 0;
}

static int parse_file_type(const char *s, char *out) {
  if (s == NULL || s[0] == '\0' || s[1] != '\0')
    return -1;

  char c = s[0];
  switch (c) {
  case 'F':
  case 'D':
  case 'L':
  case 'P':
  case 'C':
  case 'B':
  case 'S':
    *out = c;
    return 0;
  default:
    return -1;
  }
}

static int parse_perm(const char *s, unsigned int *out) {
  char *end = NULL;

  if (s == NULL || strlen(s) != 4)
    return -1;

  unsigned long v = strtoul(s, &end, 8);
  if (*s == '\0' || *end != '\0' || v > 07777)
    return -1;

  *out = (unsigned int)v;
  return 0;
}

static int parse_uintmax(const char *s, uintmax_t *out) {
  char *end = NULL;

  if (s == NULL || *s == '\0')
    return -1;

  uintmax_t v = strtoumax(s, &end, 10);
  if (*end != '\0')
    return -1;

  *out = v;
  return 0;
}

static int parse_intmax_nonnegative(const char *s, intmax_t *out) {
  char *end = NULL;

  if (s == NULL || *s == '\0')
    return -1;

  intmax_t v = strtoimax(s, &end, 10);
  if (*end != '\0' || v < 0)
    return -1;

  *out = v;
  return 0;
}

struct RecordObject {
  char file_type;
  unsigned int perm;
  uintmax_t uid;
  uintmax_t gid;
  intmax_t size;
  intmax_t time;
  char *path;
};

struct RecordObject *unpack(char *s) {
  char *fields[NFIELDS];
  if (split_record(s, fields, '|') == -1)
    return NULL;

  struct RecordObject *objp = calloc(1, sizeof(*objp));
  if (objp == NULL) {
    return NULL;
  }

  field = NONE;

  if (parse_file_type(fields[0], &objp->file_type) == -1) {
    field = FILE_TYPE;
    return objp;
  }

  if (parse_perm(fields[1], &objp->perm) == -1) {
    field = PERMISSIONS;
    return objp;
  }

  if (parse_uintmax(fields[2], &objp->uid) == -1) {
    field = UID;
    return objp;
  }

  if (parse_uintmax(fields[3], &objp->gid) == -1) {
    field = GID;
    return objp;
  }

  if (parse_intmax_nonnegative(fields[4], &objp->size) == -1) {
    field = SIZE;
    return objp;
  }

  if (parse_intmax_nonnegative(fields[5], &objp->time) == -1) {
    field = TIME;
    return objp;
  }

  if (fields[6][0] == '\0') {
    field = PATH;
    return objp;
  }

  char *path = calloc(strlen(fields[6]) + 1, sizeof(char));
  if (path == NULL)
    return NULL;

  objp->path = strcpy(path, fields[6]);

  return objp;
}

int release(struct RecordObject *obj) {
  if (obj == NULL) {
    return 0;
  }
  if (obj->path != NULL) {
    free(obj->path);
    obj->path = NULL;
  }

  free(obj);
  return 0;
}

static size_t count_lines(const char *file) {
  FILE *f = fopen(file, "r");
  if (f == NULL) {
    return 0;
  }

  size_t count = 0;
  char buf[LINEMAX];
  while (fgets(buf, sizeof(buf), f) != NULL)
    count++;

  if (ferror(f)) {
    fclose(f);
    return 0;
  }

  fclose(f);
  return count;
}

int list(const char *snapshot) {
  FILE *snapshot_file = fopen(snapshot, "r");
  if (snapshot_file == NULL) {
    return -1;
  }

  int rc = 0;
  const size_t arr_size = count_lines(snapshot);
  if (arr_size == 0) {
    rc = -1;
    goto out;
  }

  int i = 0;
  char buf[LINEMAX];
  struct RecordObject **records = calloc(arr_size, sizeof(*records));
  while (fgets(buf, sizeof(buf), snapshot_file) != NULL) {
    struct RecordObject *objp = unpack(buf);
    if (objp == NULL) {
      fprintf(stderr, "%s:%d: unparsable line\n", snapshot, i + 1);
      rc = 128;
      break;
    }

    if (field != NONE) {
      fprintf(stderr, "%s:%d: invalid %s\n", snapshot, i + 1,
              field_name(field));
      rc = 128;
      release(objp);
      break;
    }
    records[i] = objp;
    i++;
  }

  if (ferror(snapshot_file)) {
    rc = -1;
    goto out;
  }

out:
  if (snapshot_file != NULL && fclose(snapshot_file) != 0) {
    rc = -1;
  }
  if (records != NULL) {
    for (size_t i = 0; i < arr_size; i++) {
      release(records[i]);
      records[i] = NULL;
    }
    free(records);
    records = NULL;
  }
  return rc;
}
