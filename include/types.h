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
int Unpack(struct RecordObject *, char *);
int RecordObjectWrite(struct RecordObject *self, const char *path, FILE *out);
int Compare(struct RecordObject *, struct RecordObject *);
char GetFileType(const struct RecordObject *);
unsigned int GetPermissions(const struct RecordObject *);
uintmax_t GetUid(const struct RecordObject *);
uintmax_t GetGid(const struct RecordObject *);
intmax_t GetSize(const struct RecordObject *);
intmax_t GetTime(const struct RecordObject *);
const char *GetPath(const struct RecordObject *);
const char *GetTarget(const struct RecordObject *);
uintmax_t GetFingerPrint(const struct RecordObject *);
int Release(struct RecordObject *);

#endif
