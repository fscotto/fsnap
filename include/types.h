#ifndef TYPE_H
#define TYPE_H

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

const char *FieldName(enum Fields f);

struct RecordObject;

// Methods
struct RecordObject *NewRecordObject();
int RecordObjectUnpack(struct RecordObject *, char *);
int RecordObjectWrite(struct RecordObject *self, const char *path, FILE *out);
int RecordObjectCompare(struct RecordObject *, struct RecordObject *);
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
