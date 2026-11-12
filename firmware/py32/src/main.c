#include "py32f0xx.h"
#include "../../../shared/protocol/ring_frame.h"
#include "py32f002b_ll_adc.h"
#include "py32f002b_ll_bus.h"
#include "py32f002b_ll_gpio.h"
#include "py32f002b_ll_rcc.h"
#include "py32f002b_ll_tim.h"
#include "py32f002b_ll_usart.h"
#include <stdbool.h>
#include <stddef.h>
#if PINBALL_UPDATE_ENABLED
#include "../../../shared/protocol/firmware_update.h"
#include "update_flash.h"
#include <string.h>
#endif

extern void sk6812_send_frame(volatile uint32_t *bsrr, uint32_t set_mask,
                              uint32_t reset_mask, const uint32_t *early_reset);

/* Inspect this counter in the debugger. */
volatile uint32_t heartbeat_count;
enum { HALL_PORT_COUNT = 6, HALL_BASELINE_SAMPLES = 8 };
enum { PORT_IDLE_TIMEOUT_MS = 1000, PORT_IDLE_STEP_MS = 500 };
enum { SERVO_FRAME_US = 20000, SERVO_CENTER_US = 1500, SERVO_SPAN_US = 500 };
volatile uint16_t hall_adc_raw[HALL_PORT_COUNT];
volatile uint16_t hall_baseline[HALL_PORT_COUNT];
volatile bool hall_baseline_ready[HALL_PORT_COUNT];
volatile bool hall_ball_present[HALL_PORT_COUNT];
static uint32_t hall_baseline_sum[HALL_PORT_COUNT];
static uint8_t hall_sample_count[HALL_PORT_COUNT];

static const struct {
    GPIO_TypeDef *gpio;
    uint32_t pin;
    uint32_t channel;
} hall_inputs[HALL_PORT_COUNT] = {
    { GPIOB, LL_GPIO_PIN_0, LL_ADC_CHANNEL_7 },
    { GPIOB, LL_GPIO_PIN_1, LL_ADC_CHANNEL_0 },
    { GPIOA, LL_GPIO_PIN_7, LL_ADC_CHANNEL_4 },
    { GPIOA, LL_GPIO_PIN_3, LL_ADC_CHANNEL_1 },
    { GPIOA, LL_GPIO_PIN_6, LL_ADC_CHANNEL_3 },
    { GPIOA, LL_GPIO_PIN_4, LL_ADC_CHANNEL_2 },
};
static bool led_overridden[RING_LED_COUNT];
static volatile uint8_t port_types[RING_LED_COUNT];
static volatile uint16_t servo_pulse_us[RING_LED_COUNT];
static volatile uint8_t servo_high_port = RING_LED_COUNT;
static volatile bool servo_paused;
static uint32_t port_colors[RING_LED_COUNT];
static uint32_t displayed_colors[RING_LED_COUNT];
static uint32_t last_message_ms;
static volatile bool ports_idle;
static bool hall_adc_ready;
static bool sensor_monitoring;
static uint8_t monitored_port;
static uint32_t last_sensor_report;
static volatile uint32_t uptime_ms;
/* Receive the next frames while the main loop handles or forwards a packet. */
enum { RING_RX_SLOTS = 4 };
static uint8_t ring_rx[RING_RX_SLOTS][RING_MAX_LENGTH + 1];
static volatile uint8_t ring_rx_read;
static volatile uint8_t ring_rx_write;
static uint8_t ring_rx_size;
static uint32_t ring_last_rx_ms;
static bool ring_discard;
static uint8_t node_address = RING_UNASSIGNED;

void SysTick_Handler(void)
{
    ++uptime_ms;
}

void USART1_IRQHandler(void)
{
    const uint32_t status = USART1->SR;
    const uint32_t errors = USART_SR_ORE | USART_SR_FE | USART_SR_NE;
    if ((status & (USART_SR_RXNE | errors)) == 0U) {
        return;
    }
    /* Reading SR then DR clears the receive error flags. */
    const uint8_t byte = LL_USART_ReceiveData8(USART1);
    const uint32_t now = uptime_ms;
    const uint32_t gap = now - ring_last_rx_ms;
    ring_last_rx_ms = now;
    if (gap >= RING_IDLE_GAP_MS) {
        ring_rx_size = 0;
        ring_discard = false;
    }

    const uint8_t slot = ring_rx_write;
    const uint8_t next_slot = (slot + 1U) % RING_RX_SLOTS;
    if ((status & errors) != 0U || next_slot == ring_rx_read ||
        (ring_rx_size == 0U && (byte < RING_MIN_LENGTH || byte > RING_MAX_LENGTH))) {
        ring_discard = true;
    }
    if (!ring_discard) {
        ring_rx[slot][ring_rx_size++] = byte;
        if (ring_rx_size == ring_rx[slot][0] + 1U) {
            ring_rx_size = 0;
            __DMB();
            ring_rx_write = next_slot;
        }
    }
}

static const struct {
    GPIO_TypeDef *port;
    uint32_t pin;
} leds[RING_LED_COUNT] = {
    { GPIOA, LL_GPIO_PIN_0 },
    { GPIOA, LL_GPIO_PIN_1 },
    { GPIOA, LL_GPIO_PIN_5 },
    { GPIOB, LL_GPIO_PIN_2 },
    { GPIOB, LL_GPIO_PIN_7 },
    { GPIOB, LL_GPIO_PIN_3 },
};

static bool port_is_servo(size_t port)
{
    return port_types[port] >= RING_PORT_SERVO;
}

void TIM14_IRQHandler(void)
{
    const bool new_frame = LL_TIM_IsActiveFlag_UPDATE(TIM14);
    const uint8_t previous = servo_high_port;
    if (previous < RING_LED_COUNT) {
        LL_GPIO_ResetOutputPin(leds[previous].port, leds[previous].pin);
    }
    servo_high_port = RING_LED_COUNT;
    LL_TIM_ClearFlag_UPDATE(TIM14);
    LL_TIM_ClearFlag_CC1(TIM14);

    /* Send one pulse at a time. Six maximum-width pulses fit in 20 ms. */
    if (!ports_idle && !servo_paused) {
        for (unsigned port = new_frame ? 0U : previous + 1U; port < RING_LED_COUNT; ++port) {
            const uint16_t width = servo_pulse_us[port];
            if (port_is_servo(port) && width != 0U) {
                servo_high_port = port;
                LL_GPIO_SetOutputPin(leds[port].port, leds[port].pin);
                LL_TIM_OC_SetCompareCH1(TIM14, LL_TIM_GetCounter(TIM14) + width);
                LL_TIM_EnableIT_CC1(TIM14);
                return;
            }
        }
    }
    LL_TIM_DisableIT_CC1(TIM14);
}

static void configure_servo_timer(void)
{
    const uint32_t apb_prescaler = LL_RCC_GetAPB1Prescaler();
    const uint32_t pclk = __LL_RCC_CALC_PCLK1_FREQ(SystemCoreClock, apb_prescaler);
    const uint32_t timer_clock = apb_prescaler == LL_RCC_APB1_DIV_1 ? pclk : pclk * 2U;
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_TIM14);
    LL_TIM_SetPrescaler(TIM14, timer_clock / 1000000U - 1U);
    LL_TIM_SetAutoReload(TIM14, SERVO_FRAME_US - 1U);
    LL_TIM_GenerateEvent_UPDATE(TIM14);
    LL_TIM_ClearFlag_UPDATE(TIM14);
    LL_TIM_EnableIT_UPDATE(TIM14);
    NVIC_EnableIRQ(TIM14_IRQn);
    LL_TIM_EnableCounter(TIM14);
}

static void configure_node1_ports(void)
{
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA | LL_IOP_GRP1_PERIPH_GPIOB);
    for (size_t port = 0; port < HALL_PORT_COUNT; ++port) {
        LL_GPIO_SetPinPull(hall_inputs[port].gpio, hall_inputs[port].pin, LL_GPIO_PULL_NO);
        LL_GPIO_SetPinMode(hall_inputs[port].gpio, hall_inputs[port].pin, LL_GPIO_MODE_ANALOG);
    }

    for (size_t i = 0; i < sizeof leds / sizeof leds[0]; ++i) {
        GPIO_TypeDef *port = leds[i].port;
        const uint32_t pin = leds[i].pin;
        LL_GPIO_ResetOutputPin(port, pin);
        LL_GPIO_SetPinPull(port, pin, LL_GPIO_PULL_NO);
        LL_GPIO_SetPinOutputType(port, pin, LL_GPIO_OUTPUT_PUSHPULL);
        LL_GPIO_SetPinSpeed(port, pin, LL_GPIO_SPEED_FREQ_HIGH);
        LL_GPIO_SetPinMode(port, pin, LL_GPIO_MODE_OUTPUT);
        displayed_colors[i] = UINT32_MAX;
    }
}

static bool configure_hall_adc(void)
{
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_ADC1);
    LL_ADC_SetClock(ADC1, LL_ADC_CLOCK_SYNC_PCLK_DIV4);
    LL_ADC_SetResolution(ADC1, LL_ADC_RESOLUTION_12B);
    LL_ADC_SetSamplingTimeCommonChannels(ADC1, LL_ADC_SAMPLINGTIME_239CYCLES_5);
    LL_ADC_StartCalibration(ADC1);

    for (unsigned attempt = 0; attempt < 24000U; ++attempt) {
        if (!LL_ADC_IsCalibrationOnGoing(ADC1)) {
            /* Allow at least four ADC clocks before enabling. */
            for (unsigned cycle = 0; cycle < 32U; ++cycle) {
                __NOP();
            }
            return true;
        }
    }
    return false;
}

static bool show_led_color(size_t port, uint32_t grb)
{
    const uint32_t reset_mask = leds[port].pin << 16;
    uint32_t early_reset[24];
    for (size_t bit = 0; bit < 24; ++bit) {
        early_reset[bit] = (grb & (1UL << (23U - bit))) ? 0U : reset_mask;
    }

    const uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
    if (servo_high_port < RING_LED_COUNT) {
        __set_PRIMASK(interrupt_mask);
        return false;
    }
    sk6812_send_frame(&leds[port].port->BSRR, leds[port].pin, reset_mask, early_reset);
    __set_PRIMASK(interrupt_mask);
    return true;
}

static void track_hall_sample(size_t port, uint16_t sample)
{
    hall_adc_raw[port] = sample;
    if (!hall_baseline_ready[port]) {
        hall_baseline_sum[port] += sample;
        if (++hall_sample_count[port] == HALL_BASELINE_SAMPLES) {
            hall_baseline[port] = (uint16_t)((hall_baseline_sum[port] +
                                            HALL_BASELINE_SAMPLES / 2U) /
                                           HALL_BASELINE_SAMPLES);
            hall_baseline_ready[port] = true;
        }
        return;
    }

    const uint32_t baseline = hall_baseline[port];
    const uint32_t deviation = sample >= baseline ? sample - baseline : baseline - sample;
    if (hall_ball_present[port]) {
        /* Clear once the reading returns within 5% of baseline. */
        if (deviation * 20U <= baseline) {
            hall_ball_present[port] = false;
        }
    } else if (baseline != 0U && deviation * 20U > baseline) {
        hall_ball_present[port] = true;
    }
}

static bool sample_hall_input(size_t port, uint16_t *sample)
{
    if (LL_ADC_REG_IsConversionOngoing(ADC1)) {
        return false;
    }
    const uint32_t channel = hall_inputs[port].channel;
    const uint32_t channel_mask = channel & ADC_CHSELR_CHSEL;
    if (ADC1->CHSELR != channel_mask || !LL_ADC_IsEnabled(ADC1)) {
        /* This device ignores channel writes while ADEN is set. */
        LL_ADC_Disable(ADC1);
        LL_ADC_REG_SetSequencerChannels(ADC1, channel);
        if (ADC1->CHSELR != channel_mask) {
            return false;
        }
        LL_ADC_Enable(ADC1);
        /* Allow at least eight ADC clocks at PCLK divided by four. */
        for (unsigned cycle = 0; cycle < 64U; ++cycle) {
            __NOP();
        }
    }
    LL_ADC_ClearFlag_EOC(ADC1);
    LL_ADC_REG_StartConversion(ADC1);
    for (unsigned attempt = 0; attempt < 24000U; ++attempt) {
        if (LL_ADC_IsActiveFlag_EOC(ADC1)) {
            *sample = LL_ADC_REG_ReadConversionData12(ADC1);
            return true;
        }
    }
    return false;
}

static void scan_hall_inputs(void)
{
    for (size_t port = 0; port < HALL_PORT_COUNT; ++port) {
        uint16_t sample;
        if (sample_hall_input(port, &sample)) {
            track_hall_sample(port, sample);
            if (port_types[port] == RING_PORT_LED_HALL && !led_overridden[port]) {
                port_colors[port] = hall_ball_present[port] ? 0x190000U : 0x000300U;
            }
        }
    }
}

static void refresh_port_colors(size_t idle_color_index)
{
    static const uint32_t idle_colors[] = {0x001000U, 0x100000U, 0x000010U};
    for (size_t port = 0; port < RING_LED_COUNT; ++port) {
        if (!ports_idle && port_is_servo(port)) {
            continue;
        }
        const uint32_t color = ports_idle ? idle_colors[idle_color_index] : port_colors[port];
        if (displayed_colors[port] != color && show_led_color(port, color)) {
            displayed_colors[port] = color;
        }
    }
}

static void configure_ring_uart(void)
{
    const uint32_t peripheral_clock =
        __LL_RCC_CALC_PCLK1_FREQ(SystemCoreClock, LL_RCC_GetAPB1Prescaler());

    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_USART1);
    LL_USART_Disable(USART1);
    LL_USART_ConfigAsyncMode(USART1);
    LL_USART_ConfigCharacter(USART1, LL_USART_DATAWIDTH_8B,
                             LL_USART_PARITY_NONE, LL_USART_STOPBITS_1);
    LL_USART_SetHWFlowCtrl(USART1, LL_USART_HWCONTROL_NONE);
    LL_USART_SetOverSampling(USART1, LL_USART_OVERSAMPLING_16);
    LL_USART_SetBaudRate(USART1, peripheral_clock, LL_USART_OVERSAMPLING_16, 115200U);
    LL_USART_SetTransferDirection(USART1, LL_USART_DIRECTION_TX_RX);
    LL_USART_Enable(USART1);

    /* Ring TX is PB4 and RX is PB5. RX idles high if the upstream node is offline. */
    LL_GPIO_SetAFPin_0_7(GPIOB, LL_GPIO_PIN_4, LL_GPIO_AF1_USART1);
    LL_GPIO_SetPinOutputType(GPIOB, LL_GPIO_PIN_4, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_4, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_4, LL_GPIO_MODE_ALTERNATE);

    LL_GPIO_SetAFPin_0_7(GPIOB, LL_GPIO_PIN_5, LL_GPIO_AF1_USART1);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_5, LL_GPIO_PULL_UP);
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_5, LL_GPIO_MODE_ALTERNATE);
}

static void send_ring_frame(const uint8_t *frame, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        while (!LL_USART_IsActiveFlag_TXE(USART1)) {
        }
        LL_USART_TransmitData8(USART1, frame[i]);
    }
}

static void configure_sensor_monitor(uint8_t port, uint8_t enable)
{
    if (enable == 0U) {
        if (sensor_monitoring && monitored_port == port) {
            sensor_monitoring = false;
        }
        return;
    }

    if (!hall_adc_ready) {
        hall_adc_ready = configure_hall_adc();
    }
    if (hall_adc_ready) {
        monitored_port = port;
        last_sensor_report = uptime_ms;
        sensor_monitoring = true;
    }
}

static void report_sensor_sample(uint32_t now)
{
    if (!sensor_monitoring || now - last_sensor_report < RING_SENSOR_PERIOD_MS) {
        return;
    }

    last_sensor_report = now;
    uint16_t sample;
    if (sample_hall_input(monitored_port, &sample)) {
        hall_adc_raw[monitored_port] = sample;
        const uint8_t scaled = (uint8_t)(((uint32_t)sample * 255U + 2047U) / 4095U);
        uint8_t frame[RING_SENSOR_FRAME_SIZE];
        ring_build_sensor_frame(frame, node_address, RING_COORDINATOR, monitored_port, scaled);
        send_ring_frame(frame, sizeof frame);
    }
}

static uint8_t *append_color_value(uint8_t *out, uint8_t value)
{
    if (value >= 100U) {
        *out++ = (uint8_t)('0' + value / 100U);
    }
    if (value >= 10U) {
        *out++ = (uint8_t)('0' + value / 10U % 10U);
    }
    *out++ = (uint8_t)('0' + value % 10U);
    return out;
}

static void send_led_debug(uint8_t index, uint32_t grb)
{
    /* Three header bytes, eleven prefix bytes, nine digits, two separators and CRC. */
    uint8_t frame[3 + 11 + 9 + 4 + 2] = {
        0, (uint8_t)(node_address << 4), (RING_TTL << 4) | RING_DEBUG,
        'L', 'E', 'D', ' ', (uint8_t)('1' + index), ' ', 'R', 'G', 'B', ':', ' '
    };
    uint8_t *out = append_color_value(frame + 14, (uint8_t)(grb >> 8));
    *out++ = ',';
    *out++ = ' ';
    out = append_color_value(out, (uint8_t)(grb >> 16));
    *out++ = ',';
    *out++ = ' ';
    out = append_color_value(out, (uint8_t)grb);
    const size_t count = (size_t)(out - frame) + 2U;
    frame[0] = (uint8_t)(count - 1U);
    ring_write_crc(frame, count);
    send_ring_frame(frame, count);
}

static void assign_ring_address(uint8_t origin, uint8_t address_byte)
{
    const uint8_t address = address_byte & 0x0FU;
    if (address == 0U || address > RING_LAST_UNICAST) {
        return;
    }

    node_address = address;
    uint8_t frame[RING_ADDRESS_FRAME_SIZE];
    if (address < RING_LAST_UNICAST) {
        const uint8_t next_address = (address_byte & 0xF0U) | (address + 1U);
        ring_build_address_frame(frame, origin, RING_UNASSIGNED, RING_SET_ADDRESS,
                                 next_address);
        send_ring_frame(frame, sizeof frame);
    }

    /* The Node_Config payload is reserved and currently sent as zero. */
    ring_build_address_frame(frame, node_address, origin, RING_NODE_CONFIG, 0);
    send_ring_frame(frame, sizeof frame);
}

#if PINBALL_UPDATE_ENABLED
_Static_assert(PINBALL_VERSION_MAJOR >= 0 && PINBALL_VERSION_MAJOR <= 255 &&
               PINBALL_VERSION_MINOR >= 0 && PINBALL_VERSION_MINOR <= 255,
               "Firmware version fields must fit one byte");

static bool update_active;
static uint16_t update_size;

static bool write_update_page(uint32_t address, const void *data, size_t length)
{
    servo_paused = true;
    while (servo_high_port < RING_LED_COUNT) {
    }
    const bool written = update_write_page(address, data, length);
    servo_paused = false;
    return written;
}

uint32_t HAL_GetTick(void)
{
    return uptime_ms;
}

static void send_update_reply(uint8_t command, const uint8_t *payload, size_t length)
{
    uint8_t reply[10];
    const size_t count = length + 5U;
    reply[0] = (uint8_t)(count - 1U);
    reply[1] = (uint8_t)(node_address << 4);
    reply[2] = (RING_TTL << 4) | command;
    memcpy(reply + 3, payload, length);
    ring_write_crc(reply, count);
    send_ring_frame(reply, count);
}

static void handle_firmware_command(uint8_t command, const uint8_t *payload, size_t length)
{
    if (command == RING_FIRMWARE_START && length == 3 && payload[2] == 0U) {
        const uint16_t requested = ((uint16_t)payload[0] << 8) | payload[1];
        const uint16_t current = (PINBALL_VERSION_MAJOR << 8) | PINBALL_VERSION_MINOR;
        update_active = requested > current;
        update_size = 0;
        if (update_active) {
            sensor_monitoring = false;
        }
        const uint8_t reply[] = {PINBALL_VERSION_MAJOR, PINBALL_VERSION_MINOR, update_active};
        send_update_reply(command, reply, sizeof reply);
    } else if (command == RING_FIRMWARE_DATA && update_active &&
               length >= 3U && length <= 2U + RING_FIRMWARE_PAGE_BYTES) {
        const uint16_t offset = ((uint16_t)payload[0] << 8) | payload[1];
        const size_t bytes = length - 2U;
        if ((offset % RING_FIRMWARE_PAGE_BYTES) != 0U ||
            offset + bytes > RING_FIRMWARE_SLOT_BYTES) {
            return;
        }

        const uint32_t address = UPDATE_STAGE_BASE + offset;
        if (offset + bytes <= update_size &&
            memcmp((const void *)address, payload + 2, bytes) == 0) {
            send_update_reply(command, payload, 2);
        } else if (offset == update_size && write_update_page(address, payload + 2, bytes)) {
            update_size += (uint16_t)bytes;
            send_update_reply(command, payload, 2);
        }
    } else if (command == RING_FIRMWARE_APPLY && update_active &&
               length == 5U && payload[4] == 0U) {
        const uint32_t crc = firmware_crc32((const uint8_t *)UPDATE_STAGE_BASE, update_size);
        bool accepted = false;
        if (crc == firmware_read_be32(payload) &&
            update_vectors_valid(UPDATE_STAGE_BASE, update_size)) {
            update_record_t record = {UPDATE_PENDING_MAGIC, update_size, crc, 0};
            record.record_crc = firmware_crc32((const uint8_t *)&record,
                                                offsetof(update_record_t, record_crc));
            accepted = write_update_page(UPDATE_METADATA_BASE, &record, sizeof record);
        }
        uint8_t reply[5];
        firmware_write_be32(reply, crc);
        reply[4] = accepted;
        send_update_reply(command, reply, sizeof reply);
        if (accepted) {
            while (!LL_USART_IsActiveFlag_TC(USART1)) {
            }
            NVIC_SystemReset();
        }
    }
}
#endif

static void handle_ring_frame(bool led_timing_ok)
{
    const uint8_t slot = ring_rx_read;
    if (slot == ring_rx_write) {
        return;
    }

    __DMB();
    uint8_t *frame = ring_rx[slot];
    const size_t count = frame[0] + 1U;
    const uint8_t origin = frame[1] >> 4;
    const uint8_t destination = frame[1] & 0x0FU;
    const uint8_t command = frame[2] & 0x0FU;
    const uint8_t ttl = frame[2] >> 4;
    const uint16_t expected_crc = ((uint16_t)frame[count - 2] << 8) | frame[count - 1];

    if (origin != node_address && expected_crc == ring_crc16(frame, count - 2)) {
        last_message_ms = uptime_ms;
        ports_idle = false;
        if (destination == node_address) {
            if (node_address == RING_UNASSIGNED && command == RING_SET_ADDRESS &&
                count == RING_ADDRESS_FRAME_SIZE) {
                assign_ring_address(origin, frame[3]);
            } else if (node_address != RING_UNASSIGNED && origin == RING_COORDINATOR &&
                       command == RING_SET_PORT_TYPE && count == RING_PORT_TYPE_FRAME_SIZE &&
                       frame[3] < RING_LED_COUNT && frame[4] <= RING_PORT_SERVO_FEEDBACK) {
                const uint8_t port = frame[3];
                if (port_is_servo(port) && frame[4] < RING_PORT_SERVO) {
                    servo_pulse_us[port] = 0U;
                    displayed_colors[port] = UINT32_MAX;
                }
                port_types[port] = frame[4];
            } else if (node_address != RING_UNASSIGNED && origin == RING_COORDINATOR &&
                       command == RING_SET_SERVO_POSITION &&
                       count == RING_SERVO_POSITION_FRAME_SIZE &&
                       frame[3] < RING_LED_COUNT && port_is_servo(frame[3])) {
                const int position = frame[4] < 128U ? frame[4] : (int)frame[4] - 256;
                servo_pulse_us[frame[3]] = SERVO_CENTER_US +
                    position * SERVO_SPAN_US / (position < 0 ? 128 : 127);
            } else if (node_address != RING_UNASSIGNED && led_timing_ok &&
                       command == RING_SET_LED && count == RING_LED_FRAME_SIZE &&
                       frame[3] < RING_LED_COUNT && !port_is_servo(frame[3])) {
                const uint8_t index = frame[3];
                const uint32_t grb = ((uint32_t)frame[5] << 16) |
                                     ((uint32_t)frame[4] << 8) | frame[6];
                led_overridden[index] = true;
                port_colors[index] = grb;
                send_led_debug(index, grb);
            } else if (node_address != RING_UNASSIGNED && origin == RING_COORDINATOR &&
                       command == RING_MONITOR_SENSOR && count == RING_SENSOR_FRAME_SIZE &&
                       frame[3] < HALL_PORT_COUNT && frame[4] <= 1U) {
                configure_sensor_monitor(frame[3], frame[4]);
#if PINBALL_UPDATE_ENABLED
            } else if (node_address != RING_UNASSIGNED && origin == RING_COORDINATOR) {
                handle_firmware_command(command, frame + 3, count - 5U);
#endif
            }
        } else if (ttl > 1U) {
            frame[2] = (uint8_t)(((ttl - 1U) << 4) | command);
            ring_write_crc(frame, count);
            send_ring_frame(frame, count);
        }
    } else if (origin != node_address) {
        /* A bad CRC can mean the length was corrupted. Resync after an idle gap. */
        __disable_irq();
        ring_rx_size = 0;
        ring_discard = true;
        ring_rx_read = ring_rx_write;
        __enable_irq();
        return;
    }

    __DMB();
    ring_rx_read = (slot + 1U) % RING_RX_SLOTS;
}

int main(void)
{
    configure_node1_ports();
    SystemCoreClockUpdate();
    configure_ring_uart();
    configure_servo_timer();
    const bool led_timing_ok = SystemCoreClock == 24000000U;
    hall_adc_ready = PINBALL_ENABLE_HALL_ADC && led_timing_ok && configure_hall_adc();
    if (led_timing_ok) {
        /* Wait for LED power-up before sending the first frame. */
        for (uint32_t cycle = 0; cycle < 240000U; ++cycle) {
            __NOP();
        }
    }

    SysTick_Config(SystemCoreClock / 1000U);
    LL_USART_EnableIT_ERROR(USART1);
    LL_USART_EnableIT_RXNE(USART1);
    NVIC_EnableIRQ(USART1_IRQn);

    uint32_t last_refresh = uptime_ms;
    uint32_t last_idle_change = 0;
    size_t idle_color_index = 0;
    for (;;) {
        ++heartbeat_count;
        handle_ring_frame(led_timing_ok);
        const uint32_t now = uptime_ms;
        report_sensor_sample(now);
        if (!ports_idle && now - last_message_ms >= PORT_IDLE_TIMEOUT_MS) {
            ports_idle = true;
            idle_color_index = 0;
            last_idle_change = now;
        } else if (ports_idle && now - last_idle_change >= PORT_IDLE_STEP_MS) {
            idle_color_index = (idle_color_index + 1U) % 3U;
            last_idle_change = now;
        }
        if (led_timing_ok) {
            refresh_port_colors(idle_color_index);
        }
        if (!ports_idle && now - last_refresh >= 50U) {
            last_refresh = now;
            if (PINBALL_ENABLE_HALL_ADC && hall_adc_ready) {
                scan_hall_inputs();
            }
        }
    }
}
