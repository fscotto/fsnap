#ifndef HASH_H
#define HASH_H

#include <stdint.h>
#include <stdio.h>

/* Compute CRC-32 checksum of stream contents.
   Returns 0 on read error. */
uint32_t crc32(FILE *stream);

#endif
