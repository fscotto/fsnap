#ifndef WALK_H
#define WALK_H

typedef int(*operation)(const char *, void *);

int walk(const char *directory, operation op, void *context);

#endif
