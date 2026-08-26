#ifndef WALK_H
#define WALK_H

typedef int (*WalkOp)(const char *, void *);

/* Recursively walk a directory, calling op for each entry.
   Returns 0 on success, -1 on failure. Silently skips directories
   where access is denied. */
int walk(const char *directory, WalkOp op, void *context);

#endif
