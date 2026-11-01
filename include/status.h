#ifndef STATUS_H
#define STATUS_H

/* Public status 2 means the command completed with skipped entries. Parse
   errors stay internal: main maps them to public failure (1). */
enum { FSNAP_WARNINGS = 2, FSNAP_EPARSE = 128 };

#endif
