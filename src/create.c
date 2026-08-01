#define _XOPEN_SOURCE 500
#include "create.h"
#include "utility.h"
#include "walk.h"
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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
  fprintf(snapshot, "%c|%04o|%ju|%ju|%jd|%jd|%s\n", type, perm, uid, gid, size,
          time, path);

  return 0;
}

int create(const char *directory, const char *output_file) {
  if (directory == NULL || strlen(directory) == 0) {
    return -1;
  }
  if (output_file == NULL || strlen(output_file) == 0) {
    return -1;
  }

  const char *resolved = resolve_path(directory);
  if (resolved == NULL) {
    return -1;
  }
  FILE *f = fopen(output_file, "w+");
  if (f == NULL) {
    return -1;
  }

  if (walk(resolved, write_record, (void *)f) == -1) {
    return -1;
  }

  if (fclose(f) != 0) {
    return -1;
  }
  return 0;
}
