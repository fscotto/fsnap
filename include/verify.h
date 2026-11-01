#ifndef VERIFY_H
#define VERIFY_H

#include "status.h"

/* Compare a snapshot with the live tree rooted at root_fs.
   Returns 0 when they match, 1 when differences were found and reported on
   stdout, -1 on a system/IO error, and FSNAP_EPARSE on a malformed snapshot. */
int verify(const char *snapshot, const char *root_fs);

#endif
