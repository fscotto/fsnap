#ifndef LIST_H
#define LIST_H

struct RecordObject;
struct RecordObject *unpack(char *s);
int release(struct RecordObject *obj);

int list(const char *snapshot);

#endif
