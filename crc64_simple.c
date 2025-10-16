/*
 * Simple CRC64 Library Implementation
 */

#include "crc64_simple.h"
#include <string.h>

// CRC64 lookup table
static uint64_t crc64_table[256];
static int crc64_initialized = 0;

// Initialize CRC64 lookup table
void crc64_init(void) {
    if (crc64_initialized) {
        return;
    }
    
    // Use ECMA polynomial by default
    uint64_t poly = CRC64_POLY_ECMA;
    
    for (int i = 0; i < 256; i++) {
        uint64_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ poly;
            } else {
                crc >>= 1;
            }
        }
        crc64_table[i] = crc;
    }
    
    crc64_initialized = 1;
}

// Update CRC64 with additional data (internal)
static inline uint64_t crc64_update(uint64_t crc, const unsigned char *data, size_t len) {
    crc64_init();
    
    // Process data byte by byte
    for (size_t i = 0; i < len; i++) {
        crc = crc64_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return crc;
}

// Compute CRC64 for data
uint64_t crc64_compute(const unsigned char *data, size_t len) {
    return crc64_update(0, data, len);
}
