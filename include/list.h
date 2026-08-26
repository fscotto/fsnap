#ifndef LIST_H
#define LIST_H

/* List all entries in a snapshot file.
   Returns 0 on success, -1 on system/IO error, 128 on parse error. */
int list(const char *snapshot);

#endif
