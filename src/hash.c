#include "hash.h"

#define CRC32_POLY 0xEDB88320
#define BUFFER_SIZE 4096

uint32_t crc32_table[256];
int table_computed = 0;

static void init_crc32_table() {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t crc = i;
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ CRC32_POLY;
      } else {
        crc >>= 1;
      }
    }
    crc32_table[i] = crc;
  }
  table_computed = 1;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length) {
  if (!table_computed) {
    init_crc32_table();
  }

  for (size_t i = 0; i < length; i++) {
    uint8_t lookup_index = (crc ^ data[i]) & 0xFF;
    crc = (crc >> 8) ^ crc32_table[lookup_index];
  }

  return crc;
}

uint32_t crc32(FILE *stream) {
  uint8_t buffer[BUFFER_SIZE];
  size_t bytes_read;
  uint32_t crc = 0xFFFFFFFF;

  while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, stream)) > 0) {
    crc = crc32_update(crc, buffer, bytes_read);
  }

  return crc ^ 0xFFFFFFFF;
}
