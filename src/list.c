#include "list.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#ifdef LINE_MAX
#define LINEMAX LINE_MAX
#else
#define LINEMAX 2048
#endif

#define PRINT(objp)                                                            \
  printf("%c|%04o|%ju|%ju|%jd|%jd|%s", objp->file_type, objp->perm, objp->uid, \
         objp->gid, objp->size, objp->time, objp->path)

#define FIRST(s) s[0]

static int validate_record(struct RecordObject *obj) {
  // TODO implements this function
  return 0;
}

struct RecordObject {
  char file_type;
  unsigned int perm;
  uintmax_t uid;
  uintmax_t gid;
  intmax_t size;
  intmax_t time;
  char *path;
};

struct RecordObject *unpack(char *s) {
  const char *sep = "|";

  struct RecordObject *obj = malloc(sizeof(struct RecordObject));
  if (obj == NULL) {
    return NULL;
  }

  obj->file_type = FIRST(strtok(s, sep));
  obj->perm = (unsigned int)strtoul(strtok(NULL, sep), NULL, 10);
  obj->uid = strtoumax(strtok(NULL, sep), NULL, 10);
  obj->gid = strtoumax(strtok(NULL, sep), NULL, 10);
  obj->size = strtoimax(strtok(NULL, sep), NULL, 10);
  obj->time = strtoimax(strtok(NULL, sep), NULL, 10);

  const char *p = strtok(NULL, sep);
  char *path = calloc(strlen(p), sizeof(char));
  strcpy(path, p);

  obj->path = path;

  if (strtok(NULL, sep) != NULL)
    return NULL;

  if (validate_record(obj) == -1) {
    return NULL;
  }

  return obj;
}

int release(struct RecordObject *obj) {
  if (obj == NULL) {
    return 0;
  }
  if (obj->path != NULL) {
    free(obj->path);
    obj->path = NULL;
  }
  return 0;
}

static size_t count_lines(const char *file) {
  FILE *f = fopen(file, "r");
  if (f == NULL) {
    return 0;
  }

  size_t count = 0;
  char buf[LINEMAX];
  while (fgets(buf, sizeof(buf), f) != NULL)
    count++;

  if (ferror(f)) {
    fclose(f);
    return 0;
  }

  fclose(f);
  return count;
}

int list(const char *snapshot) {
  FILE *snapshot_file = fopen(snapshot, "r");
  if (snapshot_file == NULL) {
    return -1;
  }

  int rc = 0;
  const size_t arr_size = count_lines(snapshot);
  if (arr_size == 0) {
    rc = -1;
    goto out;
  }

  int i = 0;
  char buf[LINEMAX];
  struct RecordObject **records = calloc(arr_size, sizeof(struct RecordObject));
  while (fgets(buf, sizeof(buf), snapshot_file) != NULL) {
    struct RecordObject *objp = unpack(buf);
    if (objp == NULL) {
      fprintf(stderr, "[ERROR] unparsable line %d", i);
      errno = EINVAL;
      break;
    }
    records[i] = objp;
    PRINT(objp);
    i++;
  }

  if (ferror(snapshot_file)) {
    rc = -1;
    goto out;
  }

out:
  if (snapshot_file != NULL && fclose(snapshot_file) != 0) {
    rc = -1;
  }
  if (records != NULL) {
    for (size_t i = 0; i < arr_size; i++) {
      release(records[i]);
      records[i] = NULL;
    }
    free(records);
    records = NULL;
  }
  return rc;
}
