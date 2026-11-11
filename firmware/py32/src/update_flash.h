#ifndef PINBALL_UPDATE_FLASH_H
#define PINBALL_UPDATE_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UPDATE_APP_BASE       0x08001000U
#define UPDATE_STAGE_BASE     0x08003000U
#define UPDATE_METADATA_BASE  0x08005000U
#define UPDATE_PENDING_MAGIC  0x50424431U

typedef struct {
    uint32_t magic;
    uint32_t size;
    uint32_t image_crc;
    uint32_t record_crc;
} update_record_t;

bool update_erase_page(uint32_t address);
bool update_write_page(uint32_t address, const void *data, size_t size);
bool update_vectors_valid(uint32_t image_base, size_t image_size);

#endif
