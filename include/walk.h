#ifndef WALK_H
#define WALK_H

typedef int(*WalkOp)(const char *, void *);

int walk(const char *directory, WalkOp op, void *context);

#endif
