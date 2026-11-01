#include "list.h"
#include "snapshot.h"
#include <errno.h>
#include <stdlib.h>

int list(const char *snapshot) {
  struct RecordObject **records = NULL;
  int count = 0;
  int rc = snapshot_load(snapshot, &records, &count);
  const int saved_errno = errno;
  snapshot_release(records, count);
  errno = saved_errno;
  return rc;
}
