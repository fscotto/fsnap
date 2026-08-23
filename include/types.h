#ifndef TYPE_H
#define TYPE_H

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

const char *field_name(enum Fields f);

struct RecordObject;
struct RecordObject *unpack(char *s);
int release(struct RecordObject *obj);

#endif
