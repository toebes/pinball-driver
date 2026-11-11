#ifndef PINBALL_UPDATE_WIRE_H
#define PINBALL_UPDATE_WIRE_H

#include <stddef.h>
#include <stdint.h>

enum {
    RING_FIRMWARE_START = 10,
    RING_FIRMWARE_DATA = 11,
    RING_FIRMWARE_APPLY = 12,
    RING_FIRMWARE_PAGE_BYTES = 128,
    RING_FIRMWARE_SLOT_BYTES = 8192
};

static inline uint32_t firmware_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return ~crc;
}

static inline uint32_t firmware_read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static inline void firmware_write_be32(uint8_t *data, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        data[i] = (uint8_t)(value >> (24U - 8U * i));
    }
}

#endif
