#ifndef SCAN_H
#define SCAN_H

/* Recursively scan a directory, printing all entries.
   Returns 0 on success, FSNAP_WARNINGS (2) if entries were skipped,
   -1 on failure. */
int scan(const char *directory);

#endif
