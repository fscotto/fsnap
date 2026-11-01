/* Compile create.c and snapshot.c with selected libc names redirected here.
   No preload, linker extensions, privileges, or second filesystem required. */
#include "create.h"
#include "diff.h"
#include "list.h"
#include "snapshot.h"
#include "verify.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int test_fsync(int fd);
int test_rename(const char *from, const char *to);
int test_fclose(FILE *stream);
int test_fstat(int fd, struct stat *st);

enum Fault { NO_FAULT, SYNC, CLOSE, RENAME, EXDEV_OK, EXDEV_SYNC,
             EXDEV_RENAME, GROW, SHRINK, STAT };
static enum Fault fault;
static int syncs, renames, closes, stats, failed, testno;
static const char *input = "input.fsnap";

static void reset(enum Fault value) {
  fault = value;
  syncs = renames = closes = stats = 0;
}

static void check(int condition, const char *description) {
  printf("%s %d - %s\n", condition ? "ok" : "not ok", ++testno, description);
  if (!condition)
    failed++;
}

int test_fsync(int fd) {
  syncs++;
  if ((fault == SYNC && syncs == 1) || (fault == EXDEV_SYNC && syncs == 2)) {
    errno = ENOSPC;
    return -1;
  }
  return fsync(fd);
}

int test_rename(const char *from, const char *to) {
  renames++;
  if ((fault == EXDEV_OK || fault == EXDEV_SYNC || fault == EXDEV_RENAME) &&
      renames == 1) {
    errno = EXDEV;
    return -1;
  }
  if (fault == RENAME || (fault == EXDEV_RENAME && renames == 2)) {
    errno = EACCES;
    return -1;
  }
  return rename(from, to);
}

int test_fclose(FILE *stream) {
  closes++;
  int rc = fclose(stream);
  if (fault == CLOSE && closes == 1) {
    errno = EIO;
    return EOF;
  }
  return rc;
}

int test_fstat(int fd, struct stat *st) {
  stats++;
  if (fault == STAT) {
    errno = EIO;
    return -1;
  }
  if (stats == 2 && fault == GROW) {
    FILE *out = fopen(input, "a");
    if (out == NULL)
      return -1;
    if (fputs("X|bad\n", out) == EOF || fclose(out) != 0)
      return -1;
  }
  if (stats == 2 && fault == SHRINK && truncate(input, 0) == -1)
    return -1;
  return fstat(fd, st);
}

static void write_file(const char *path, const char *text) {
  FILE *stream = fopen(path, "w");
  if (stream == NULL || fputs(text, stream) == EOF || fclose(stream) != 0) {
    perror(path);
    exit(2);
  }
}

static int old_output(void) {
  char buf[64];
  FILE *stream = fopen("output.fsnap", "r");
  if (stream == NULL)
    return 0;
  int same = fgets(buf, sizeof(buf), stream) != NULL && strcmp(buf, "old\n") == 0;
  fclose(stream);
  return same;
}

static int no_temporaries(void) {
  DIR *dir = opendir(".");
  if (dir == NULL)
    return 0;
  struct dirent *entry;
  int clean = 1;
  while ((entry = readdir(dir)) != NULL)
    if (strncmp(entry->d_name, "snapshot_", 9) == 0)
      clean = 0;
  closedir(dir);
  return clean;
}

int main(void) {
  const char *tmpdir = getenv("TMPDIR");
  if (tmpdir == NULL || *tmpdir == '\0')
    tmpdir = "/tmp";
  size_t size = strlen(tmpdir) + sizeof("/fsnap-faults.XXXXXX");
  char *work = malloc(size);
  if (work == NULL)
    return 2;
  snprintf(work, size, "%s/fsnap-faults.XXXXXX", tmpdir);
  if (mkdtemp(work) == NULL) {
    perror("fault test directory");
    free(work);
    return 2;
  }
  char *absolute = realpath(work, NULL);
  free(work);
  work = absolute;
  if (work == NULL || chdir(work) == -1 || mkdir("tree", 0700) == -1) {
    perror("fault test fixture");
    free(work);
    return 2;
  }
  /* Keep all scratch data in the fixture, regardless of the runner's TMPDIR. */
  if (setenv("TMPDIR", ".", 1) == -1)
    return 2;
  write_file("tree/file", "contents\n");

  const enum Fault create_faults[] = {SYNC, CLOSE, RENAME, EXDEV_SYNC, EXDEV_RENAME};
  const char *names[] = {"flush/sync failure preserves old output",
                         "close failure preserves old output",
                         "rename failure preserves old output",
                         "cross-device copy sync failure preserves old output",
                         "cross-device final rename failure preserves old output"};
  for (size_t i = 0; i < sizeof(create_faults) / sizeof(create_faults[0]); i++) {
    write_file("output.fsnap", "old\n");
    reset(create_faults[i]);
    int rc = create("tree", "output.fsnap");
    int err = errno;
    reset(NO_FAULT);
    check(rc == -1 && err != 0 && old_output() && no_temporaries(), names[i]);
  }
  reset(EXDEV_OK);
  int rc = create("tree", "output.fsnap");
  check(rc == 0 && renames == 2 && syncs == 2 && no_temporaries(),
        "EXDEV copies to a sibling and atomically renames it");
  reset(NO_FAULT);
  check(list("output.fsnap") == 0, "cross-device result is a valid snapshot");

  for (int command = 0; command < 4; command++) {
    write_file(input, "F|0644|0|0|0|1||0|/f\n");
    reset(GROW);
    struct RecordObject **records = NULL;
    int count = 0;
    switch (command) {
    case 0: rc = snapshot_load(input, &records, &count); break;
    case 1: rc = list(input); break;
    case 2: rc = diff(input, "output.fsnap"); break;
    default: rc = verify(input, "tree"); break;
    }
    check(rc == -1 && errno == EAGAIN && records == NULL && count == 0,
          "reader rejects growth after parsing rather than accepting a prefix");
  }
  const enum Fault read_faults[] = {SHRINK, CLOSE, STAT};
  for (size_t i = 0; i < sizeof(read_faults) / sizeof(read_faults[0]); i++) {
    write_file(input, "F|0644|0|0|0|1||0|/f\n");
    reset(read_faults[i]);
    struct RecordObject **records = NULL;
    int count = 0;
    rc = snapshot_load(input, &records, &count);
    check(rc == -1 && errno != 0 && records == NULL && count == 0,
          "reader cleans up on shrink, close error, or stat error");
  }
  reset(NO_FAULT);
  unlink(input);
  unlink("output.fsnap");
  unlink("tree/file");
  rmdir("tree");
  if (chdir("/") == -1 || rmdir(work) == -1) {
    perror("fault test cleanup");
    failed++;
  }
  free(work);
  printf("1..%d\n# %d fault tests passed, %d failed\n", testno, testno - failed, failed);
  return failed ? 1 : 0;
}
