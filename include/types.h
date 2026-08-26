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

/* Allocate a new RecordObject. Returns NULL on allocation failure,
   with errno set to ENOMEM. */
struct RecordObject *RecordObjectNew(void);

/* Unpack a pipe-delimited record string into self.
   Returns 0 on success, -1 on failure. On failure errno is EINVAL when the
   record is malformed and something else, ENOMEM in practice, when the
   failure is the parser's own; only in the first case is *err meaningful.
   If err is non-NULL and the offending field could be identified, sets *err
   to that field. */
int RecordObjectUnpack(struct RecordObject *self, char *s, enum Fields *err);

/* Populate self by stat-ing the file at path and writing to out.
   Returns the number of bytes written on success, -1 on failure. */
int RecordObjectWrite(struct RecordObject *self, const char *path, FILE *out);

/* Compare two RecordObjects field by field.
   Returns 0 if equal, and an ordering like strcmp otherwise: negative if self
   sorts first, positive if other does. On NULL self returns 128 and on NULL
   other 1; those are ordering sentinels, unrelated to the FSNAP_EPARSE command
   status that happens to share the value.
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

/* Free self and all owned resources. Does nothing if self is NULL. */
void RecordObjectRelease(struct RecordObject *self);

#endif
