#define _POSIX_C_SOURCE 200809L
#include "list.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

  /* Only the path may contain an escaped delimiter, and it is the last field,
     so the six separators found above are always the real ones. Validating
     what is left is the unescaper's job. */
  fields[NFIELDS - 1] = p;

  return 0;
}

/* Undo the escaping create applies: \\ -> \, \| -> |, \n -> newline. A bare
   delimiter, an unknown escape, or a trailing backslash means the record is
   malformed; that is reported with EINVAL to keep it apart from allocation
   failure. */
static char *unescape_path(const char *s, char delim) {
  const size_t len = strlen(s);

  char *out = calloc(len + 1, sizeof(char));
  if (out == NULL)
    return NULL;

  size_t j = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] == delim)
      goto malformed;

    if (s[i] != '\\') {
      out[j++] = s[i];
      continue;
    }

    /* s[len] is the terminator, so a trailing backslash lands on the
       default case rather than reading past the end. */
    i++;
    switch (s[i]) {
    case '\\':
      out[j++] = '\\';
      break;
    case 'n':
      out[j++] = '\n';
      break;
    default:
      if (s[i] != delim)
        goto malformed;
      out[j++] = delim;
      break;
    }
  }

  return out;

malformed:
  free(out);
  errno = EINVAL;
  return NULL;
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
  case '?':
    *out = c;
    return 0;
  default:
    return -1;
  }
}

/* strtol and friends accept leading whitespace, a sign, and wrap negative
   input around for the unsigned variants. Reject anything that is not a bare
   run of decimal digits before converting. */
static int only_digits(const char *s, int base) {
  const char last = (base == 8) ? '7' : '9';

  if (s == NULL || *s == '\0')
    return 0;

  for (const char *p = s; *p != '\0'; p++) {
    if (*p < '0' || *p > last)
      return 0;
  }
  return 1;
}

static int parse_perm(const char *s, unsigned int *out) {
  char *end = NULL;

  if (s == NULL || strlen(s) != 4 || !only_digits(s, 8))
    return -1;

  errno = 0;
  unsigned long v = strtoul(s, &end, 8);
  if (*end != '\0' || errno == ERANGE || v > 07777)
    return -1;

  *out = (unsigned int)v;
  return 0;
}

static int parse_uintmax(const char *s, uintmax_t *out) {
  char *end = NULL;

  if (!only_digits(s, 10))
    return -1;

  errno = 0;
  uintmax_t v = strtoumax(s, &end, 10);
  if (*end != '\0' || errno == ERANGE)
    return -1;

  *out = v;
  return 0;
}

static int parse_intmax_nonnegative(const char *s, intmax_t *out) {
  char *end = NULL;

  if (!only_digits(s, 10))
    return -1;

  errno = 0;
  intmax_t v = strtoimax(s, &end, 10);
  if (*end != '\0' || errno == ERANGE || v < 0)
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

  errno = 0;
  char *path = unescape_path(fields[6], '|');
  if (path == NULL) {
    if (errno == EINVAL) {
      field = PATH;
      return objp;
    }
    release(objp);
    return NULL;
  }

  objp->path = path;

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

static int count_lines(const char *file) {
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
      release(records[j]);
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
