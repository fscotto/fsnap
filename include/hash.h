#ifndef HASH_H
#define HASH_H

#include <stdint.h>
#include <stdio.h>

/* Compute the CRC-32 checksum of the stream's remaining contents into *out.
   Returns 0 on success, -1 on read error, leaving *out untouched. The result
   is reported through an out parameter because 0 is a perfectly good checksum
   and would otherwise be indistinguishable from a failure. */
int crc32(FILE *stream, uint32_t *out);

#endif
