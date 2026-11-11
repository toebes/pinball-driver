#include "py32f0xx.h"

#include "../src/update_flash.h"
#include "../../../shared/protocol/firmware_update.h"

static volatile uint32_t uptime_ms;

void SysTick_Handler(void)
{
    ++uptime_ms;
}

uint32_t HAL_GetTick(void)
{
    return uptime_ms;
}

static bool activate_staged_image(void)
{
    const update_record_t *record = (const update_record_t *)UPDATE_METADATA_BASE;
    if (record->magic != UPDATE_PENDING_MAGIC ||
        record->record_crc != firmware_crc32((const uint8_t *)record,
                                              offsetof(update_record_t, record_crc))) {
        return true;
    }

    const uint32_t size = record->size;
    const uint32_t expected_crc = record->image_crc;
    if (!update_vectors_valid(UPDATE_STAGE_BASE, size) ||
        firmware_crc32((const uint8_t *)UPDATE_STAGE_BASE, size) != expected_crc) {
        return false;
    }

    for (uint32_t offset = 0; offset < size; offset += RING_FIRMWARE_PAGE_BYTES) {
        if (!update_write_page(UPDATE_APP_BASE + offset,
                               (const void *)(UPDATE_STAGE_BASE + offset),
                               RING_FIRMWARE_PAGE_BYTES)) {
            return false;
        }
    }
    if (firmware_crc32((const uint8_t *)UPDATE_APP_BASE, size) != expected_crc) {
        return false;
    }
    return update_erase_page(UPDATE_METADATA_BASE);
}

static __attribute__((naked, noreturn)) void jump_to_application(
    uint32_t stack __attribute__((unused)), uint32_t reset __attribute__((unused)))
{
    __asm volatile("msr msp, r0\n"
                   "bx r1");
}

int main(void)
{
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);
    if (activate_staged_image() && update_vectors_valid(UPDATE_APP_BASE, RING_FIRMWARE_SLOT_BYTES)) {
        const volatile uint32_t *vectors = (const volatile uint32_t *)UPDATE_APP_BASE;
        SysTick->CTRL = 0;
        SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
        SCB->VTOR = UPDATE_APP_BASE;
        __DSB();
        __ISB();
        jump_to_application(vectors[0], vectors[1]);
    }

    /* Keep the resident image running when the application is absent or invalid. */
    for (;;) {
        __WFI();
    }
}
