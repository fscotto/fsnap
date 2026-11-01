#include "create.h"
#include "status.h"
#include "types.h"
#include "utility.h"
#include "walk.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

struct CreateContext {
  FILE *stream;
  const char *temporary;
  const char *destination;
};

static int write_record(const char *path, void *context) {
  struct CreateContext *ctx = context;
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  size_t n = strlen(base);
  if ((n >= 6 && strcasecmp(base + n - 6, ".fsnap") == 0) ||
      strcmp(path, ctx->temporary) == 0 || strcmp(path, ctx->destination) == 0)
    return 0;

  struct RecordObject *r = RecordObjectNew();
  if (r == NULL)
    return -1;
  int ret = RecordObjectWrite(r, path, ctx->stream);
  const int err = errno;
  RecordObjectRelease(r);
  if (ret < 0 && err == EACCES) {
    /* No bytes are emitted until all metadata and the checksum are ready. */
    fprintf(stderr, "access denied: %s (entry skipped)\n", path);
    return FSNAP_WARNINGS;
  }
  errno = err;
  return ret < 0 ? -1 : 0;
}

/* Create an absolute temporary name, so the walker can exclude it even when
   TMPDIR is relative or reached through a symlink. Only successful mkstemp
   names are owned by the caller and may be unlinked. */
static int temporary_file(const char *directory, char **name) {
  char *resolved = resolve_path(directory);
  if (resolved == NULL)
    return -1;
  char *path = strconcat(resolved, 1, "/snapshot_XXXXXX");
  free(resolved);
  if (path == NULL)
    return -1;
  int fd = mkstemp(path);
  if (fd == -1) {
    const int err = errno;
    free(path);
    errno = err;
  } else {
    *name = path;
  }
  return fd;
}

int create(const char *directory, const char *output_file) {
  if (directory == NULL || directory[0] == '\0' ||
      output_file == NULL || output_file[0] == '\0') {
    errno = EINVAL;
    return -1;
  }

  int rc = -1;
  char *resolved = NULL, *parent = NULL, *destination = NULL;
  char *temporary = NULL, *sibling = NULL, *output = NULL;
  FILE *stream = NULL;
  int fd = -1;

  resolved = resolve_path(directory);
  if (resolved == NULL)
    goto cleanup;
  output = strdup(output_file);
  if (output == NULL)
    goto cleanup;
  char *base = strrchr(output, '/');
  if (base != NULL) {
    *base++ = '\0';
    parent = resolve_path(output[0] ? output : "/");
  } else {
    base = output;
    parent = resolve_path(".");
  }
  if (parent == NULL)
    goto cleanup;
  if (*base == '\0') {
    errno = EINVAL;
    goto cleanup;
  }
  destination = strconcat(parent, 2, "/", base);
  if (destination == NULL)
    goto cleanup;

  /* Never replace a device, directory, FIFO, or symlink with a snapshot. In
     particular, do not follow a destination symlink and truncate its target. */
  struct stat st;
  if (lstat(destination, &st) == 0) {
    if (!S_ISREG(st.st_mode)) {
      errno = EINVAL;
      goto cleanup;
    }
  } else if (errno != ENOENT) {
    goto cleanup;
  }

  const char *tmpdir = getenv("TMPDIR");
  fd = temporary_file(tmpdir != NULL && *tmpdir != '\0' ? tmpdir : parent,
                      &temporary);
  if (fd == -1)
    goto cleanup;
  stream = fdopen(fd, "w");
  if (stream == NULL)
    goto cleanup;
  fd = -1; /* stream now owns the descriptor */
  if (fputs("FSNAP|1\n", stream) == EOF)
    goto cleanup;

  struct CreateContext ctx = {stream, temporary, destination};
  int status = walk(resolved, write_record, &ctx);
  if (status == -1)
    goto cleanup;

  if (fflush(stream) == EOF || fsync(fileno(stream)) == -1)
    goto cleanup;
  int close_status = fclose(stream);
  stream = NULL;
  if (close_status != 0)
    goto cleanup;

  if (rename(temporary, destination) == -1) {
    if (errno != EXDEV)
      goto cleanup;
    /* TMPDIR may be on another filesystem. Copy only to a private sibling,
       never to the published destination; close and sync before rename. */
    fd = temporary_file(parent, &sibling);
    if (fd == -1)
      goto cleanup;
    if (copy(temporary, sibling) == -1 || fsync(fd) == -1)
      goto cleanup;
    close_status = close(fd);
    fd = -1;
    if (close_status == -1 || rename(sibling, destination) == -1)
      goto cleanup;
    free(sibling);
    sibling = NULL;
    if (unlink(temporary) == -1)
      goto cleanup;
  }
  free(temporary);
  temporary = NULL;
  rc = status;

cleanup:;
  const int saved_errno = errno;
  if (stream != NULL)
    fclose(stream);
  if (fd != -1)
    close(fd);
  if (temporary != NULL)
    unlink(temporary);
  if (sibling != NULL)
    unlink(sibling);
  free(temporary);
  free(sibling);
  free(output);
  free(destination);
  free(parent);
  free(resolved);
  errno = saved_errno;
  return rc;
}
