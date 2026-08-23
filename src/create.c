#define _POSIX_C_SOURCE 1
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#include "create.h"
#include "hash.h"
#include "utility.h"
#include "walk.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

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

static int write_record(const char *path, void *context) {
  // skip snapshot files
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  size_t n = strlen(base);
  if (n >= 6 && strcasecmp(base + n - 6, ".fsnap") == 0)
    return 0;

  FILE *snapshot = (FILE *)context;
  struct stat st;
  if (lstat(path, &st) == -1) {
    return -1;
  }

  char type = file_type(st.st_mode);
  unsigned int perm = (unsigned int)(st.st_mode & 07777);
  uintmax_t uid = (uintmax_t)st.st_uid;
  uintmax_t gid = (uintmax_t)st.st_gid;
  intmax_t size = (intmax_t)st.st_size;
  intmax_t time = (intmax_t)st.st_mtime;
  char *s = sanitize_path(path);
  if (s == NULL)
    return -1;

  char *target = NULL;
  if (type == 'L') {
    size_t bufsiz = ((size_t)(st.st_size + 1));

    if (st.st_size == 0)
      bufsiz = (size_t)sysconf(_PC_PATH_MAX);

    target = calloc(bufsiz, sizeof(char));
    if (target == NULL)
      goto failure;

    if (readlink(path, target, bufsiz) == -1)
      goto failure;
  }

  FILE *f = NULL;
  uint32_t hash = 0;
  if (!S_ISDIR(st.st_mode)) {
    if ((f = fopen(path, "r")) == NULL)
      goto failure;

    hash = crc32(f);
  }

  int ret =
      fprintf(snapshot, "%c|%04o|%ju|%ju|%jd|%jd|%s|%u|%s\n", type, perm, uid,
              gid, size, time, (target == NULL ? "" : target), hash, s);

  free(s);

  if (target != NULL)
    free(target);

  if (f != NULL)
    fclose(f);

  return ret < 0 ? -1 : 0;

failure:
  free(s);

  if (target != NULL)
    free(target);

  if (f != NULL)
    fclose(f);

  return -1;
}

int create(const char *directory, const char *output_file) {
  if (directory == NULL || strlen(directory) == 0) {
    errno = EINVAL;
    return -1;
  }
  if (output_file == NULL || strlen(output_file) == 0) {
    errno = EINVAL;
    return -1;
  }

  char *resolved = resolve_path(directory);
  if (resolved == NULL) {
    return -1;
  }

  int rc = 0;
  char template[] = "/tmp/snapshot_XXXXXX";
  int fd = mkstemp(template);
  if (fd == -1) {
    free(resolved);
    return -1;
  }

  FILE *tmp = fdopen(fd, "w+");
  if (tmp == NULL) {
    close(fd);
    rc = -1;
    goto out;
  }

  if (walk(resolved, write_record, (void *)tmp) == -1) {
    rc = -1;
    goto out;
  }

  if (fflush(tmp) == EOF) {
    rc = -1;
    goto out;
  }

  // FIXME: It doesn't atomic copy, using rename
  if (copy(template, output_file) == -1) {
    rc = -1;
    goto out;
  }

out:;
  /* A buffered write can fail for the first time here, so fclose owns errno
     when nothing had failed before it. */
  const int saved_errno = errno;
  free(resolved);
  unlink(template);
  if (tmp != NULL && fclose(tmp) != 0 && rc == 0)
    return -1;
  if (rc == -1)
    errno = saved_errno;
  return rc;
}
