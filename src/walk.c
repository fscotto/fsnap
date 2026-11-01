#include "stack.h"
#include "utility.h"
#include "walk.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *const blacklist[] = {".", ".."};

static int ignore_file(const char *name) {
  for (size_t i = 0; i < sizeof(blacklist) / sizeof(blacklist[0]); i++) {
    if (strcmp(name, blacklist[i]) == 0)
      return 1;
  }
  return 0;
}

/* Report whether path is a real directory. d_type is a fast path that neither
   follows symbolic links nor needs another syscall; when the filesystem does
   not provide it (DT_UNKNOWN), fall back to lstat, which also never follows
   links. Returns 1 for a directory, 0 for anything else, -1 on error. */
static int is_directory(const char *path, unsigned char type) {
#ifdef DT_UNKNOWN
  if (type != DT_UNKNOWN)
    return type == DT_DIR;
#endif
  struct stat st;
  if (lstat(path, &st) == -1)
    return -1;
  return S_ISDIR(st.st_mode) != 0;
}

int walk(const char *directory, WalkOp op, void *context) {
  char *base = NULL;
  DIR *dirp = NULL;
  int status = 0;

  struct Stack *stack = StackNew();
  if (stack == NULL)
    goto failed;

  dirp = opendir(directory);
  if (dirp == NULL)
    goto failed;

  /* The stack owns the char *paths of the subdirectories still to inspect.
     At most one DIR is open at a time, so a deep tree cannot exhaust the
     file-descriptor limit. base is the path of the directory currently being
     read; NULL means the root was passed directly, so its storage is not
     owned. */
  for (;;) {
    struct dirent *direntp;
    for (;;) {
      errno = 0;
      direntp = readdir(dirp);
      if (direntp == NULL)
        break;
      if (ignore_file(direntp->d_name))
        continue;

      char *path =
          strconcat(base == NULL ? directory : base, 2, "/", direntp->d_name);
      if (path == NULL)
        goto failed;

      const int type = is_directory(path, direntp->d_type);
      if (type == -1) {
        free(path);
        goto failed;
      }

      int ret = op(path, context);
      if (ret == -1) {
        free(path);
        goto failed;
      }
      if (ret == FSNAP_WARNINGS)
        status = FSNAP_WARNINGS;

      if (type == 1) {
        /* Report the directory itself, then keep it pending for descent.
           The stack now owns path; do not free it here. */
        if (StackPush(stack, path) == -1) {
          free(path);
          goto failed;
        }
      } else {
        free(path);
      }
    }

    if (errno != 0)
      goto failed;

    int close_status = closedir(dirp);
    dirp = NULL;
    if (close_status == -1)
      goto failed;
    free(base);
    base = NULL;

    /* Open the next pending subdirectory, skipping the unreadable ones.
       The previous handle is already closed, so only one stays open. */
    while (base == NULL) {
      if (StackEmpty(stack))
        goto success;
      base = StackPop(stack);
      dirp = opendir(base);
      if (dirp != NULL)
        break;
      if (errno != EACCES) {
        free(base);
        base = NULL;
        goto failed;
      }
      fprintf(stderr, "access denied: %s\n", base);
      status = FSNAP_WARNINGS;
      free(base);
      base = NULL;
    }
  }

success:;
  if (dirp != NULL)
    closedir(dirp);
  free(base);
  while (stack != NULL && !StackEmpty(stack)) {
    free(StackPop(stack));
  }
  if (stack != NULL)
    StackRelease(stack);
  return status;

failed:;
  const int saved_errno = errno;
  if (dirp != NULL)
    closedir(dirp);
  free(base);
  while (stack != NULL && !StackEmpty(stack)) {
    free(StackPop(stack));
  }
  if (stack != NULL)
    StackRelease(stack);
  errno = saved_errno;
  return -1;
}
