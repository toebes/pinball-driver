#include <Arduino.h>
#include <lvgl.h>
#include "ring_frame.h"

namespace {
constexpr uint8_t brightness = 25;
constexpr size_t buttons_per_led = 3;
struct Preset {
    const char *label;
    uint32_t swatch;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

const Preset presets[] = {
    {"Red",     0xC62828, brightness, 0, 0},
    {"Green",   0x16834A, 0, brightness, 0},
    {"Blue",    0x2455C3, 0, 0, brightness},
};

const Preset &preset_for_button(size_t button)
{
    return presets[button % buttons_per_led];
}

lv_obj_t *status_label;
uint8_t config_rx[RING_MAX_LENGTH + 1];
size_t config_rx_size;
uint32_t last_rx_ms;
bool discard_rx;

void log_ring_tx(const uint8_t *frame, size_t size)
{
    Serial.print("Ring TX:");
    for (size_t i = 0; i < size; ++i) {
        Serial.printf(" %02X", static_cast<unsigned>(frame[i]));
    }
    Serial.println();
}

void start_address_assignment(lv_event_t *)
{
    uint8_t frame[RING_ADDRESS_FRAME_SIZE];
    ring_build_address_frame(frame, RING_COORDINATOR, RING_UNASSIGNED,
                             RING_SET_ADDRESS, RING_NODE_1);
    if (Serial1.write(frame, sizeof frame) == sizeof frame) {
        log_ring_tx(frame, sizeof frame);
        lv_label_set_text(status_label, "Address assignment sent");
    } else {
        lv_label_set_text(status_label, "UART send failed");
    }
}

void send_color(lv_event_t *event)
{
    const size_t button = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    const Preset &preset = preset_for_button(button);
    const uint8_t index = button / buttons_per_led;
    uint8_t frame[RING_LED_FRAME_SIZE];
    ring_build_set_led(frame, index, preset.red, preset.green, preset.blue);
    if (Serial1.write(frame, sizeof frame) != sizeof frame) {
        lv_label_set_text(status_label, "UART send failed");
        return;
    }
    lv_label_set_text_fmt(status_label, "Sent LED%u: %s",
                          static_cast<unsigned>(index + 1), preset.label);
    log_ring_tx(frame, sizeof frame);
}
}

void led_control_screen_begin()
{
    Serial1.begin(115200, SERIAL_8N1, 18, 17);

    lv_obj_t *screen = lv_scr_act();
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Pinball Driver");
#if LV_FONT_MONTSERRAT_24
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
#endif
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 17);

    for (size_t i = 0; i < RING_LED_COUNT * buttons_per_led; ++i) {
        const size_t row = i / buttons_per_led;
        const size_t column = i % buttons_per_led;
        const Preset &preset = preset_for_button(i);
        lv_obj_t *button = lv_btn_create(screen);
        lv_obj_set_size(button, 208, 51);
        lv_obj_align(button, LV_ALIGN_TOP_MID, (static_cast<int>(column) - 1) * 232,
                     58 + static_cast<int>(row) * 60);
        lv_obj_set_style_bg_color(button, lv_color_hex(preset.swatch), 0);
        lv_obj_set_style_text_color(button, lv_color_white(), 0);
        lv_obj_add_event_cb(button, send_color, LV_EVENT_CLICKED, reinterpret_cast<void *>(i));

        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text_fmt(label, "LED%u  %s", static_cast<unsigned>(row + 1),
                              preset.label);
        lv_obj_center(label);
    }

    lv_obj_t *assign = lv_btn_create(screen);
    lv_obj_set_size(assign, 178, 36);
    lv_obj_align(assign, LV_ALIGN_TOP_RIGHT, -64, 10);
    lv_obj_add_event_cb(assign, start_address_assignment, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *assign_label = lv_label_create(assign);
    lv_label_set_text(assign_label, "Assign Addresses");
    lv_obj_center(assign_label);

    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "Ready");
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -24);
}

void led_control_screen_poll()
{
    if (status_label == nullptr) {
        return;
    }
    if (millis() - last_rx_ms >= RING_IDLE_GAP_MS) {
        config_rx_size = 0;
        discard_rx = false;
    }

    while (Serial1.available() > 0) {
        const uint8_t byte = static_cast<uint8_t>(Serial1.read());
        last_rx_ms = millis();
        if (discard_rx) {
            continue;
        }
        if (config_rx_size == 0 && (byte < RING_MIN_LENGTH || byte > RING_MAX_LENGTH)) {
            discard_rx = true;
            continue;
        }
        config_rx[config_rx_size++] = byte;
        if (config_rx_size != static_cast<size_t>(config_rx[0]) + 1U) {
            continue;
        }
        const uint8_t origin = config_rx[1] >> 4;
        const uint8_t dest = config_rx[1] & 0x0FU;
        const uint8_t command = config_rx[2] & 0x0FU;
        const uint16_t crc = (static_cast<uint16_t>(config_rx[config_rx_size - 2]) << 8) |
                             config_rx[config_rx_size - 1];
        if (origin != RING_COORDINATOR) {
            if (crc != ring_crc16(config_rx, config_rx_size - 2)) {
                discard_rx = true;
            } else if (dest == RING_COORDINATOR && command == RING_NODE_CONFIG &&
                       origin <= RING_LAST_UNICAST && config_rx_size == RING_ADDRESS_FRAME_SIZE) {
                lv_label_set_text_fmt(status_label, "Node %u assigned",
                                      static_cast<unsigned>(origin));
                Serial.printf("Node %u config: %02X\n", static_cast<unsigned>(origin),
                              static_cast<unsigned>(config_rx[3]));
            }
        }
        config_rx_size = 0;
    }
}
