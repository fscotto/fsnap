#ifndef WALK_H
#define WALK_H

typedef int(*operation)(const char *);

int walk(const char *directory, operation op);

#endif
