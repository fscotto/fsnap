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

const char *RecordObjectFieldName(enum Fields f);

struct RecordObject;

/* Allocate a new RecordObject. Returns NULL on allocation failure. */
struct RecordObject *RecordObjectNew(void);

/* Unpack a pipe-delimited record string into self.
   Returns 0 on success, -1 on failure. If err is non-NULL, sets *err to
   the field that failed to parse. */
int RecordObjectUnpack(struct RecordObject *self, char *s, enum Fields *err);

/* Populate self by stat-ing the file at path and writing to out.
   Returns the number of bytes written on success, -1 on failure. */
int RecordObjectWrite(struct RecordObject *self, const char *path, FILE *out);

/* Compare two RecordObjects field by field.
   Returns 0 if equal, a non-zero difference otherwise.
   On NULL self returns 128, on NULL other returns 1.
   If err is non-NULL, sets *err to the first differing field. */
int RecordObjectCompare(const struct RecordObject *self,
                        const struct RecordObject *other, enum Fields *err);

/* Accessors */
char RecordObjectFileType(const struct RecordObject *self);
unsigned int RecordObjectPermissions(const struct RecordObject *self);
uintmax_t RecordObjectUid(const struct RecordObject *self);
uintmax_t RecordObjectGid(const struct RecordObject *self);
intmax_t RecordObjectSize(const struct RecordObject *self);
intmax_t RecordObjectTime(const struct RecordObject *self);
const char *RecordObjectPath(const struct RecordObject *self);
const char *RecordObjectTarget(const struct RecordObject *self);
uintmax_t RecordObjectFingerPrint(const struct RecordObject *self);

/* Free self and all owned resources. Returns 0, or -1 if self is NULL. */
int RecordObjectRelease(struct RecordObject *self);

#endif
