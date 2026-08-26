#ifndef HASH_H
#define HASH_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Compute the CRC-32 checksum of a memory buffer. Cannot fail, so the result
   is returned directly. Used for a symlink, whose content is the target string
   readlink reports rather than anything reachable through the filesystem. */
uint32_t crc32_buffer(const void *data, size_t length);

/* Compute the CRC-32 checksum of the stream's remaining contents into *out.
   Returns 0 on success, -1 on read error, leaving *out untouched. The result
   is reported through an out parameter because 0 is a perfectly good checksum
   and would otherwise be indistinguishable from a failure. */
int crc32(FILE *stream, uint32_t *out);

#endif
