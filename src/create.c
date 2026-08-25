#define _POSIX_C_SOURCE 1
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#include "create.h"
#include "types.h"
#include "utility.h"
#include "walk.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static int write_record(const char *path, void *context) {
  // skip snapshot files
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  size_t n = strlen(base);
  if (n >= 6 && strcasecmp(base + n - 6, ".fsnap") == 0)
    return 0;

  struct RecordObject *r = NewRecordObject();
  if (r == NULL)
    return -1;
  int ret = RecordObjectWrite(r, path, (FILE *)context);
  RecordObjectRelease(r);
  return ret < 0 ? -1 : 0;
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
