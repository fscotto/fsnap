#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include "types.h"
#include "status.h"

/* Read every record in a single pass. Supports FSNAP|1 and legacy headerless
   snapshots. On success ownership passes to the caller; on failure outputs
   remain empty. A regular file changed during the read fails with EAGAIN. */
int snapshot_load(const char *file, struct RecordObject ***out, int *count);
void snapshot_release(struct RecordObject **records, int count);

#endif
