#include "create.h"
#include "scan.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "%s: command not found [scan|create]\n", argv[0]);
    return EXIT_FAILURE;
  }

  // I check the argument command exists
  errno = 0;
  const char *cmd = argv[1];
  if (strcmp(cmd, "scan") == 0) {
    if (argc < 3) {
      fprintf(stderr, "%s: command %s expected param [directory]\n", argv[0],
              cmd);
      return EXIT_FAILURE;
    }
    const char *directory = argv[2];
    if (scan(directory) == -1) {
      const int err = errno;
      fprintf(stderr, "scan failed: %s\n", strerror(err));
      return EXIT_FAILURE;
    }
  } else if (strcmp(cmd, "create") == 0) {
    if (argc < 4) {
      fprintf(stderr,
              "%s: command %s expected params [directory output_file]\n",
              argv[0], cmd);
      return EXIT_FAILURE;
    }
    const char *directory = argv[2];
    const char *output_file = argv[3];
    if (create(directory, output_file) == -1) {
      const int err = errno;
      fprintf(stderr, "create failed: %s\n", strerror(err));
      return EXIT_FAILURE;
    }
  } else {
    fprintf(stderr, "command %s is not valid\n", cmd);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
