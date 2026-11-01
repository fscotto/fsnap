#ifndef WALK_H
#define WALK_H

#include "status.h"

typedef int (*WalkOp)(const char *, void *);

/* Recursively walk a directory, calling op for each entry.
   Returns 0 on success, FSNAP_WARNINGS when a callback warns or a child
   directory is skipped with EACCES, -1 on failure. An unreadable root fails.
   A callback returns 0, FSNAP_WARNINGS, or -1. */
int walk(const char *directory, WalkOp op, void *context);

#endif
