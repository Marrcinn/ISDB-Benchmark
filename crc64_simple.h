/*
 * Simple CRC64 Library Header
 * 
 */

#ifndef CRC64_SIMPLE_H
#define CRC64_SIMPLE_H

#include <stdint.h>
#include <stddef.h>

// CRC64 polynomial constant (ECMA-182)
#define CRC64_POLY_ECMA 0x42F0E1EBA9EA3693ULL

// Initialize CRC64 tables (call once at startup)
void crc64_init(void);

// Compute CRC64 checksum for data
uint64_t crc64_compute(const unsigned char *data, size_t len);

#endif // CRC64_SIMPLE_H
