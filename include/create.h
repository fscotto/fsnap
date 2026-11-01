#ifndef CREATE_H
#define CREATE_H

/* Create a snapshot of a directory tree.
   Returns 0 on success, FSNAP_WARNINGS (2) if entries were skipped,
   -1 on failure. */
int create(const char *directory, const char *output_file);

#endif
