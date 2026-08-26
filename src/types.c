#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#include "hash.h"
#include "types.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NFIELDS 9

static const char *field_names[] = {
    "file type", "permissions", "uid",         "gid",  "size",
    "time",      "target",      "fingerprint", "path", "none"};

const char *RecordObjectFieldName(enum Fields f) {
  return (f <= NONE) ? field_names[f] : "unknown";
}

/*======================= static functions ===================================*/

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

static char file_type(mode_t mode) {
  if (S_ISREG(mode))
    return 'F';
  if (S_ISDIR(mode))
    return 'D';
  if (S_ISLNK(mode))
    return 'L';
  if (S_ISFIFO(mode))
    return 'P';
  if (S_ISCHR(mode))
    return 'C';
  if (S_ISBLK(mode))
    return 'B';
  if (S_ISSOCK(mode))
    return 'S';

  return '?';
}

static char *sanitize_path(const char *path) {
  size_t len = strlen(path);
  size_t escapes = 0;
  for (size_t i = 0; i < len; i++) {
    if (path[i] == '|' || path[i] == '\\' || path[i] == '\n')
      escapes++;
  }

  char *s = calloc(len + escapes + 1, sizeof(char));
  if (s == NULL)
    return NULL;

  for (size_t i = 0, j = 0; i < len; i++) {
    switch (path[i]) {
    case '\\':
      s[j++] = '\\';
      s[j++] = '\\';
      break;
    case '|':
      s[j++] = '\\';
      s[j++] = '|';
      break;
    case '\n':
      s[j++] = '\\';
      s[j++] = 'n';
      break;
    default:
      s[j++] = path[i];
      break;
    }
  }
  return s;
}

/*========================== RecordObject ====================================*/

struct RecordObject {
  char file_type;
  unsigned int perm;
  uintmax_t uid;
  uintmax_t gid;
  intmax_t size;
  intmax_t time;
  char *path;
  char *target; /* only for symlink */
  uintmax_t fingerprint;
};

struct RecordObject *RecordObjectNew() {
  struct RecordObject *ret = calloc(1, sizeof(*ret));
  if (ret == NULL)
    return NULL;
  ret->path = NULL;
  ret->target = NULL;
  return ret;
}

int RecordObjectUnpack(struct RecordObject *self, char *s, enum Fields *err) {
  char *fields[NFIELDS];
  if (split_record(s, fields, '|') == -1)
    return -1;

  if (parse_file_type(fields[0], &self->file_type) == -1) {
    if (err)
      *err = FILE_TYPE;
    return -1;
  }

  if (parse_perm(fields[1], &self->perm) == -1) {
    if (err)
      *err = PERMISSIONS;
    return -1;
  }

  if (parse_uintmax(fields[2], &self->uid) == -1) {
    if (err)
      *err = UID;
    return -1;
  }

  if (parse_uintmax(fields[3], &self->gid) == -1) {
    if (err)
      *err = GID;
    return -1;
  }

  if (parse_intmax_nonnegative(fields[4], &self->size) == -1) {
    if (err)
      *err = SIZE;
    return -1;
  }

  if (parse_intmax_nonnegative(fields[5], &self->time) == -1) {
    if (err)
      *err = TIME;
    return -1;
  }

  errno = 0;
  char *target = unescape_path(fields[6], '|');
  if (target == NULL) {
    if (errno == EINVAL) {
      if (err)
        *err = TARGET;
      return -1;
    }
    return -1;
  }

  self->target = target;

  if (parse_uintmax(fields[7], &self->fingerprint) == -1) {
    if (err)
      *err = FINGERPRINT;
    return -1;
  }

  if (fields[8][0] == '\0') {
    if (err)
      *err = PATH;
    return -1;
  }

  errno = 0;
  char *path = unescape_path(fields[8], '|');
  if (path == NULL) {
    if (errno == EINVAL) {
      if (err)
        *err = PATH;
      return -1;
    }
    return -1;
  }

  self->path = path;

  return 0;
}

int RecordObjectCompare(const struct RecordObject *self,
                        const struct RecordObject *other,
                        enum Fields *err) {
  if (self == NULL)
    return 128;
  if (other == NULL)
    return 1;

  int exit_code = 0;
  if (self->file_type != other->file_type) {
    exit_code = ((int)(self->file_type - other->file_type));
    if (err)
      *err = FILE_TYPE;
  } else if (self->perm != other->perm) {
    exit_code = ((int)(self->perm - other->perm));
    if (err)
      *err = PERMISSIONS;
  } else if (self->uid != other->uid) {
    exit_code = ((int)(self->uid - other->uid));
    if (err)
      *err = UID;
  } else if (self->gid != other->gid) {
    exit_code = ((int)(self->gid - other->gid));
    if (err)
      *err = GID;
  } else if (self->size != other->size) {
    exit_code = ((int)(self->size - other->size));
    if (err)
      *err = SIZE;
  } else if (self->time != other->time) {
    exit_code = ((int)(self->time - other->time));
    if (err)
      *err = TIME;
  } else if (self->fingerprint != other->fingerprint) {
    exit_code = ((int)(self->fingerprint - other->fingerprint));
    if (err)
      *err = FINGERPRINT;
  } else {
    int retp = strcmp(self->path, other->path);
    if (retp != 0) {
      exit_code = retp;
      if (err)
        *err = PATH;
    } else {
      int rett = strcmp(self->target, other->target);
      if (rett != 0) {
        exit_code = rett;
        if (err)
          *err = TARGET;
      }
    }
  }

  return exit_code;
}

int RecordObjectWrite(struct RecordObject *self, const char *path, FILE *out) {
  int rc = -1;
  struct stat st;
  char *s = NULL;
  char *target = NULL;
  FILE *f = NULL;

  if (lstat(path, &st) == -1)
    goto cleanup;

  self->file_type = file_type(st.st_mode);
  self->perm = (unsigned int)(st.st_mode & 07777);
  self->uid = (uintmax_t)st.st_uid;
  self->gid = (uintmax_t)st.st_gid;
  self->size = (intmax_t)st.st_size;
  self->time = (intmax_t)st.st_mtime;
  s = sanitize_path(path);
  if (s == NULL)
    goto cleanup;
  if (self->path != NULL)
    free(self->path);
  self->path = s;
  s = NULL;

  if (self->file_type == 'L') {
    size_t bufsiz = ((size_t)(st.st_size + 1));

    if (st.st_size == 0)
      bufsiz = (size_t)sysconf(_PC_PATH_MAX);

    target = calloc(bufsiz, sizeof(char));
    if (target == NULL)
      goto cleanup;

    if (readlink(path, target, bufsiz) == -1)
      goto cleanup;
  }
  if (self->target != NULL)
    free(self->target);
  self->target = target;
  target = NULL;

  uint32_t hash = 0;
  if (!S_ISDIR(st.st_mode)) {
    if ((f = fopen(path, "r")) == NULL)
      goto cleanup;

    hash = crc32(f);
    if (ferror(f) || fclose(f) != 0) {
      f = NULL;
      goto cleanup;
    }
    f = NULL;
  }
  self->fingerprint = hash;

  rc = fprintf(out, "%c|%04o|%ju|%ju|%jd|%jd|%s|%ju|%s\n", self->file_type,
               self->perm, self->uid, self->gid, self->size, self->time,
               self->target == NULL ? "" : self->target, self->fingerprint,
               self->path);

cleanup:
  if (s != NULL)
    free(s);
  if (target != NULL)
    free(target);
  if (f != NULL)
    fclose(f);
  return rc;
}

int RecordObjectRelease(struct RecordObject *self) {
  if (self == NULL)
    return -1;
  if (self->path != NULL) {
    free(self->path);
    self->path = NULL;
  }
  if (self->target != NULL) {
    free(self->target);
    self->target = NULL;
  }
  free(self);
  return 0;
}

/* Accessors */
char RecordObjectFileType(const struct RecordObject *self) {
  return self->file_type;
}
unsigned int RecordObjectPermissions(const struct RecordObject *self) {
  return self->perm;
}
uintmax_t RecordObjectUid(const struct RecordObject *self) { return self->uid; }
uintmax_t RecordObjectGid(const struct RecordObject *self) { return self->gid; }
intmax_t RecordObjectSize(const struct RecordObject *self) { return self->size; }
intmax_t RecordObjectTime(const struct RecordObject *self) { return self->time; }
const char *RecordObjectPath(const struct RecordObject *self) {
  return self->path;
}
const char *RecordObjectTarget(const struct RecordObject *self) {
  return self->target;
}
uintmax_t RecordObjectFingerPrint(const struct RecordObject *self) {
  return self->fingerprint;
}
