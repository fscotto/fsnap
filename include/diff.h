#ifndef DIFF_H
#define DIFF_H

/* Compare two snapshot files and print added/deleted/modified entries.
   Returns 0 on success, -1 on system/IO error, 128 on parse error. */
int diff(const char *file1, const char *file2);

#endif
