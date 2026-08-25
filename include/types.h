#ifndef TYPES_H
#define TYPES_H

#include <inttypes.h>
#include <stdio.h>

enum Fields {
  FILE_TYPE,
  PERMISSIONS,
  UID,
  GID,
  SIZE,
  TIME,
  TARGET,
  FINGERPRINT,
  PATH,
  NONE,
};

extern enum Fields field;

const char *RecordObjectFieldName(enum Fields f);

struct RecordObject;

/* Methods */
struct RecordObject *RecordObjectNew();
int RecordObjectUnpack(struct RecordObject *, char *);
int RecordObjectWrite(struct RecordObject *self, const char *path, FILE *out);
int RecordObjectCompare(const struct RecordObject *, const struct RecordObject *);
char RecordObjectFileType(const struct RecordObject *);
unsigned int RecordObjectPermissions(const struct RecordObject *);
uintmax_t RecordObjectUid(const struct RecordObject *);
uintmax_t RecordObjectGid(const struct RecordObject *);
intmax_t RecordObjectSize(const struct RecordObject *);
intmax_t RecordObjectTime(const struct RecordObject *);
const char *RecordObjectPath(const struct RecordObject *);
const char *RecordObjectTarget(const struct RecordObject *);
uintmax_t RecordObjectFingerPrint(const struct RecordObject *);
int RecordObjectRelease(struct RecordObject *);

#endif
