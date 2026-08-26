#ifndef STATUS_H
#define STATUS_H

/* Command return values.

   0 is success and -1 a system error, with errno set. FSNAP_EPARSE says the
   input itself was malformed, which is the snapshot's problem rather than the
   system's: errno carries nothing useful and the command has already written a
   snapshot:line: diagnostic to stderr. main maps every failure to
   EXIT_FAILURE, so the distinction is internal for now. */
enum { FSNAP_EPARSE = 128 };

#endif
