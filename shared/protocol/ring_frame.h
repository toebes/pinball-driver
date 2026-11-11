#ifndef PINBALL_RING_FRAME_H
#define PINBALL_RING_FRAME_H

#include <stddef.h>
#include <stdint.h>

enum {
    RING_COORDINATOR = 0,
    RING_NODE_1 = 1,
    RING_LAST_UNICAST = 13,
    RING_MULTICAST = 14,
    RING_UNASSIGNED = 15,
    RING_ASSIGNMENT_TTL = 15,
    RING_SET_ADDRESS = 1,
    RING_NODE_CONFIG = 2,
    RING_ADDRESS_FRAME_SIZE = 6,
    RING_TTL = 5,
    RING_SET_LED = 4,
    RING_LED_COUNT = 6,
    RING_MIN_LENGTH = 4,
    RING_MAX_LENGTH = 134,
    RING_LED_FRAME_SIZE = 9,
    RING_IDLE_GAP_MS = 20
};

static inline uint16_t ring_crc16(const uint8_t *bytes, size_t count)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < count; ++i) {
        crc ^= (uint16_t)bytes[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U));
        }
    }
    return crc;
}

static inline void ring_write_crc(uint8_t *frame, size_t size)
{
    const uint16_t crc = ring_crc16(frame, size - 2);
    frame[size - 2] = (uint8_t)(crc >> 8);
    frame[size - 1] = (uint8_t)crc;
}

static inline void ring_build_address_frame(uint8_t *frame, uint8_t source,
                                            uint8_t destination, uint8_t command,
                                            uint8_t payload)
{
    frame[0] = RING_ADDRESS_FRAME_SIZE - 1;
    frame[1] = (uint8_t)((source << 4) | destination);
    frame[2] = (RING_ASSIGNMENT_TTL << 4) | command;
    frame[3] = payload;
    ring_write_crc(frame, RING_ADDRESS_FRAME_SIZE);
}

static inline void ring_build_set_led(uint8_t *frame, uint8_t index,
                                     uint8_t red, uint8_t green, uint8_t blue)
{
    frame[0] = RING_LED_FRAME_SIZE - 1;
    frame[1] = (RING_COORDINATOR << 4) | RING_NODE_1;
    frame[2] = (RING_TTL << 4) | RING_SET_LED;
    frame[3] = index;
    frame[4] = red;
    frame[5] = green;
    frame[6] = blue;
    ring_write_crc(frame, RING_LED_FRAME_SIZE);
}

#endif
