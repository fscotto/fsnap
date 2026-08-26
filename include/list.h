#ifndef LIST_H
#define LIST_H

#include "status.h"

/* List all entries in a snapshot file.
   Returns 0 on success, -1 on system/IO error, FSNAP_EPARSE on parse error. */
int list(const char *snapshot);

#endif
