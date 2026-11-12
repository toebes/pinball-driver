#include "update_flash.h"
#include "../../../shared/protocol/firmware_update.h"
#include "py32f0xx_hal.h"
#include <string.h>

_Static_assert(FLASH_PAGE_SIZE == RING_FIRMWARE_PAGE_BYTES, "Unexpected flash page size");

static bool writable_page(uint32_t address)
{
    return (address % FLASH_PAGE_SIZE) == 0U &&
           ((address >= UPDATE_APP_BASE && address <= UPDATE_METADATA_BASE) ||
            address == PORT_SETTINGS_BASE || address == PORT_SETTINGS_BASE + FLASH_PAGE_SIZE);
}

static bool erase_unlocked(uint32_t address)
{
    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase = FLASH_TYPEERASE_PAGEERASE;
    erase.PageAddress = address;
    erase.NbPages = 1;
    uint32_t error;
    if (HAL_FLASH_Erase(&erase, &error) != HAL_OK) {
        return false;
    }
    const volatile uint32_t *page = (const volatile uint32_t *)address;
    for (size_t i = 0; i < FLASH_PAGE_SIZE / sizeof(uint32_t); ++i) {
        if (page[i] != UINT32_MAX) {
            return false;
        }
    }
    return true;
}

bool update_erase_page(uint32_t address)
{
    if (!writable_page(address) || HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }
    const bool erased = erase_unlocked(address);
    HAL_FLASH_Lock();
    return erased;
}

bool update_write_page(uint32_t address, const void *data, size_t size)
{
    if (!writable_page(address) || size > FLASH_PAGE_SIZE) {
        return false;
    }
    uint32_t page[FLASH_PAGE_SIZE / sizeof(uint32_t)];
    memset(page, 0xFF, sizeof page);
    memcpy(page, data, size);
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }
    const bool programmed = erase_unlocked(address) &&
                            HAL_FLASH_PageProgram(address, page) == HAL_OK;
    HAL_FLASH_Lock();
    return programmed && memcmp((const void *)address, page, sizeof page) == 0;
}

bool update_vectors_valid(uint32_t image_base, size_t image_size)
{
    if (image_size < 8 || image_size > RING_FIRMWARE_SLOT_BYTES) {
        return false;
    }
    const volatile uint32_t *vectors = (const volatile uint32_t *)image_base;
    const uint32_t stack = vectors[0];
    const uint32_t reset = vectors[1];
    const uint32_t entry = reset & ~1UL;
    return stack > SRAM_BASE && stack <= SRAM_BASE + 3072U && (stack & 7U) == 0U &&
           (reset & 1U) != 0U && entry >= UPDATE_APP_BASE &&
           entry - UPDATE_APP_BASE < image_size;
}
