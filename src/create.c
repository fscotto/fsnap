#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#include "create.h"
#include "utility.h"
#include "walk.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>

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

  // Write record in the stream file
  return fprintf(snapshot, "%c|%04o|%ju|%ju|%jd|%jd|%s\n", type, perm, uid, gid,
                 size, time, path) < 0
             ? -1
             : 0;
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

  if (copy(template, output_file) == -1) {
    rc = -1;
    goto out;
  }

out:
  free(resolved);
  unlink(template);
  if (tmp != NULL && fclose(tmp) != 0) {
    rc = -1;
  }
  return rc;
}
