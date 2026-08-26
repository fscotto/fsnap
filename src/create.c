#define _POSIX_C_SOURCE 200809L
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
  /* skip snapshot files */
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  size_t n = strlen(base);
  if (n >= 6 && strcasecmp(base + n - 6, ".fsnap") == 0)
    return 0;

  struct RecordObject *r = RecordObjectNew();
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

  int rc = -1;
  char *resolved = NULL;
  char template[] = "/tmp/snapshot_XXXXXX";
  int fd = -1;
  FILE *tmp = NULL;

  resolved = resolve_path(directory);
  if (resolved == NULL)
    goto cleanup;

  fd = mkstemp(template);
  if (fd == -1)
    goto cleanup;

  tmp = fdopen(fd, "w+");
  if (tmp == NULL) {
    close(fd);
    goto cleanup;
  }

  if (walk(resolved, write_record, (void *)tmp) == -1)
    goto cleanup;

  if (fflush(tmp) == EOF)
    goto cleanup;

  /* FIXME: It doesn't atomic copy, using rename */
  if (copy(template, output_file) == -1)
    goto cleanup;

  rc = 0;

cleanup:;
  /* An error that happened before the cleanup owns errno; one raised by the
     cleanup itself owns it only when nothing had failed yet. A buffered write
     can fail for the first time in fclose, so that errno must survive. */
  const int saved_errno = errno;
  int cleanup_errno = 0;

  free(resolved);
  /* Only mkstemp turns the XXXXXX suffix into a real name. If it failed,
     template still holds the literal and unlink would target whatever file
     happens to carry that name. */
  if (fd != -1)
    unlink(template);
  if (tmp != NULL && fclose(tmp) != 0)
    cleanup_errno = errno;

  if (rc == -1) {
    errno = saved_errno;
    return -1;
  }
  if (cleanup_errno != 0) {
    errno = cleanup_errno;
    return -1;
  }
  return 0;
}
