#include "verify.h"
#include "status.h"
#include "snapshot.h"
#include "types.h"
#include "utility.h"
#include "walk.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* Verification findings are printed as code<TAB>path, the same shape diff
   uses. A finding is not an error: the snapshot was read and the filesystem
   answered every question. Only a failure to do either returns -1. */
struct VerifyContext {
  struct RecordObject **records;
  int count;
  int differences;
};

static int order_by_path(const void *o1, const void *o2) {
  const struct RecordObject *r1 = *(const struct RecordObject *const *)o1;
  const struct RecordObject *r2 = *(const struct RecordObject *const *)o2;
  return strcmp(RecordObjectPath(r1), RecordObjectPath(r2));
}

static int find_record(struct RecordObject *const *records, int count,
                       const char *path) {
  int lo = 0;
  int hi = count - 1;

  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const int c = strcmp(path, RecordObjectPath(records[mid]));
    if (c == 0)
      return 1;
    if (c < 0)
      hi = mid - 1;
    else
      lo = mid + 1;
  }
  return 0;
}

/* create() ignores snapshot files so that a snapshot inside the tree does not
   end up in its own successor. verify() has to ignore the same names, or the
   snapshot it is reading would be reported as a newly added file. */
static int is_snapshot_name(const char *path) {
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  const size_t n = strlen(base);
  return n >= 6 && strcasecmp(base + n - 6, ".fsnap") == 0;
}

static int report(char code, const char *path, int *differences) {
  if (fprintf(stdout, "%c\t%s\n", code, path) < 0)
    return -1;
  *differences = 1;
  return 0;
}

static int report_access(const char *path, int err, int *differences) {
  if (fprintf(stdout, "E\t%s\t%s\n", path, strerror(err)) < 0)
    return -1;
  *differences = 1;
  return 0;
}

static int mark_new(const char *path, void *context) {
  struct VerifyContext *ctx = context;

  if (is_snapshot_name(path))
    return 0;

  if (!find_record(ctx->records, ctx->count, path))
    return report('A', path, &ctx->differences);

  return 0;
}

static int same_type(char type, mode_t mode) {
  switch (type) {
  case 'F':
    return S_ISREG(mode) != 0;
  case 'D':
    return S_ISDIR(mode) != 0;
  case 'L':
    return S_ISLNK(mode) != 0;
  case 'P':
    return S_ISFIFO(mode) != 0;
  case 'C':
    return S_ISCHR(mode) != 0;
  case 'B':
    return S_ISBLK(mode) != 0;
  case 'S':
    return S_ISSOCK(mode) != 0;
  case '?':
    return !S_ISREG(mode) && !S_ISDIR(mode) && !S_ISLNK(mode) &&
           !S_ISFIFO(mode) && !S_ISCHR(mode) && !S_ISBLK(mode) &&
           !S_ISSOCK(mode);
  default:
    return 0;
  }
}

/* readlink() does not terminate its buffer and silently truncates when the
   target does not fit. The reported st_size is not reliable on every
   filesystem, so fall back to the limit of the filesystem holding the link. */
static char *read_target(const char *path, const struct stat *st) {
  size_t bufsiz = (size_t)st->st_size + 1;

  if (st->st_size == 0) {
    errno = 0;
    const long limit = pathconf(path, _PC_PATH_MAX);
    if (limit <= 0) {
      if (errno != 0)
        return NULL;
      bufsiz = PATH_MAX;
    } else {
      bufsiz = (size_t)limit;
    }
  }

  char *target = calloc(bufsiz, sizeof(char));
  if (target == NULL) {
    errno = ENOMEM;
    return NULL;
  }

  const ssize_t n = readlink(path, target, bufsiz);
  if (n == -1) {
    const int saved_errno = errno;
    free(target);
    errno = saved_errno;
    return NULL;
  }
  if ((size_t)n >= bufsiz) {
    free(target);
    errno = ENAMETOOLONG;
    return NULL;
  }
  target[n] = '\0';
  return target;
}

static int verify_target(const struct RecordObject *record,
                         const struct stat *st, int *differences) {
  const char *path = RecordObjectPath(record);
  char *target = read_target(path, st);
  if (target == NULL) {
    switch (errno) {
    case EACCES:
    case ENOENT:
    case ENOTDIR:
    case ELOOP:
    case EINVAL:
    case ENAMETOOLONG:
      return report_access(path, errno, differences);
    default:
      return -1;
    }
  }

  const char *stored = RecordObjectTarget(record);
  if (stored == NULL)
    stored = "";

  int rc = 0;
  if (strcmp(target, stored) != 0)
    rc = report('L', path, differences);

  free(target);
  return rc;
}

/* Compare one stored record with the live entry at the same path. A record
   whose path no longer exists is a deletion; the errors that mean "there is
   nothing usable here" are reported as findings, with their own reason, while
   an unexpected lstat() failure is propagated as a system error. */
static int verify_record(const struct RecordObject *record, int *differences) {
  const char *path = RecordObjectPath(record);
  struct stat st;

  if (lstat(path, &st) == -1) {
    switch (errno) {
    case ENOENT:
      return report('D', path, differences);
    case EACCES:
    case ENOTDIR:
    case ELOOP:
    case ENAMETOOLONG:
      return report_access(path, errno, differences);
    default:
      return -1;
    }
  }

  const char type = RecordObjectFileType(record);

  if (!same_type(type, st.st_mode))
    return report('T', path, differences);

  /* A symlink's size is the length of its target, and its mtime changes when
     the link is rebuilt. The target string is the specific answer, so it is
     checked before either and the size is left to it. */
  if (type == 'L') {
    if (RecordObjectTarget(record) != NULL) {
      const int rc = verify_target(record, &st, differences);
      if (rc != 0)
        return rc;
    }
    return 0;
  }

  if ((intmax_t)st.st_size != RecordObjectSize(record))
    return report('S', path, differences);

  if ((unsigned int)(st.st_mode & 07777) != RecordObjectPermissions(record))
    return report('P', path, differences);

  if ((uintmax_t)st.st_uid != RecordObjectUid(record) ||
      (uintmax_t)st.st_gid != RecordObjectGid(record))
    return report('O', path, differences);

  if ((intmax_t)st.st_mtime != RecordObjectTime(record))
    return report('M', path, differences);

  if (RecordObjectVersion(record)) {
    if ((uintmax_t)st.st_dev != RecordObjectDevice(record) ||
        (uintmax_t)st.st_ino != RecordObjectInode(record))
      return report('I', path, differences);
    if ((uintmax_t)st.st_nlink != RecordObjectLinks(record))
      return report('H', path, differences);
  }

  return 0;
}

int verify(const char *snapshot, const char *root_fs) {
  if (snapshot == NULL || snapshot[0] == '\0' || root_fs == NULL ||
      root_fs[0] == '\0') {
    errno = EINVAL;
    return -1;
  }

  int rc = -1;
  char *resolved = NULL;
  struct RecordObject **records = NULL;
  int count = 0;
  int differences = 0;

  resolved = resolve_path(root_fs);
  if (resolved == NULL)
    goto cleanup;

  rc = snapshot_load(snapshot, &records, &count);
  if (rc != 0)
    goto cleanup;

  if (records != NULL)
    qsort(records, (size_t)count, sizeof(records[0]), order_by_path);

  struct VerifyContext ctx = {records, count, differences};
  int walk_status = walk(resolved, mark_new, &ctx);
  if (walk_status == -1) {
    rc = -1;
    goto cleanup;
  }
  differences = ctx.differences;

  for (int i = 0; i < count; i++) {
    if (verify_record(records[i], &differences) == -1) {
      rc = -1;
      goto cleanup;
    }
  }

  /* stdout is block-buffered when redirected, so a write error can surface
     only here. */
  if (fflush(stdout) == EOF) {
    rc = -1;
    goto cleanup;
  }

  rc = differences ? 1 : walk_status;

cleanup:;
  const int saved_errno = errno;
  snapshot_release(records, count);
  free(resolved);
  if (rc == -1)
    errno = saved_errno;
  return rc;
}
