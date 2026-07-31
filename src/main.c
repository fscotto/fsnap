#include "scan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "command not found [scan]\n");
    return EXIT_FAILURE;
  }

  // I check the argument command exists
  errno = 0;
  const char *cmd = argv[1];
  if (strcmp(cmd, "scan") == 0) {
    const char *directory = argv[2];
    if (scan(directory) == -1) {
      const int err = errno;
      fprintf(stderr, "scan failed: %s\n", strerror(err));
      return EXIT_FAILURE;
    }
  } else {
    fprintf(stderr, "command %s is not valid\n", cmd);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
