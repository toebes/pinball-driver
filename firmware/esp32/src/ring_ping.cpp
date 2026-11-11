#include <Arduino.h>
#include <lvgl.h>
#include "ring_frame.h"
#include "firmware_update.h"
#include "py32_update_image.h"

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

#if PINBALL_HAS_UPDATE_IMAGE
enum class UpdateStep { Idle, Start, Data, Apply, BootWait, Address, Verify };
UpdateStep update_step = UpdateStep::Idle;
lv_obj_t *update_node_selector;
uint8_t update_node;
size_t image_offset;
size_t chunk_size;
uint32_t image_crc;
uint32_t deadline;
unsigned retries;
uint8_t outgoing[RING_MAX_LENGTH + 1];
size_t outgoing_size;

void finish_update(const char *message)
{
    update_step = UpdateStep::Idle;
    lv_label_set_text(status_label, message);
}

void transmit_update(uint8_t destination, uint8_t command, const uint8_t *payload,
                     size_t length, uint8_t ttl = RING_TTL)
{
    outgoing_size = length + 5;
    outgoing[0] = static_cast<uint8_t>(outgoing_size - 1);
    outgoing[1] = destination;
    outgoing[2] = (ttl << 4) | command;
    memcpy(outgoing + 3, payload, length);
    ring_write_crc(outgoing, outgoing_size);
    Serial1.write(outgoing, outgoing_size);
    retries = 0;
    deadline = millis() + 750;
}

void request_version()
{
    const uint8_t payload[] = {image_major, image_minor, 0};
    transmit_update(update_node, RING_FIRMWARE_START, payload, sizeof payload);
}

void send_next_chunk()
{
    if (image_offset == sizeof py32_update_image) {
        update_step = UpdateStep::Apply;
        uint8_t payload[5] = {};
        firmware_write_be32(payload, image_crc);
        transmit_update(update_node, RING_FIRMWARE_APPLY, payload, sizeof payload);
        lv_label_set_text(status_label, "Checking staged image...");
        return;
    }
    update_step = UpdateStep::Data;
    const size_t remaining = sizeof py32_update_image - image_offset;
    chunk_size = remaining > RING_FIRMWARE_PAGE_BYTES ? RING_FIRMWARE_PAGE_BYTES : remaining;
    uint8_t payload[2 + RING_FIRMWARE_PAGE_BYTES];
    payload[0] = static_cast<uint8_t>(image_offset >> 8);
    payload[1] = static_cast<uint8_t>(image_offset);
    memcpy(payload + 2, py32_update_image + image_offset, chunk_size);
    transmit_update(update_node, RING_FIRMWARE_DATA, payload, chunk_size + 2);
    lv_label_set_text_fmt(status_label, "Updating node %u: %u%%",
                          static_cast<unsigned>(update_node),
                          static_cast<unsigned>(image_offset * 100 / sizeof py32_update_image));
}

void start_update(lv_event_t *)
{
    if (update_step != UpdateStep::Idle) {
        return;
    }
    update_node = lv_dropdown_get_selected(update_node_selector) + 1;
    image_offset = 0;
    image_crc = firmware_crc32(py32_update_image, sizeof py32_update_image);
    update_step = UpdateStep::Start;
    lv_label_set_text_fmt(status_label, "Checking node %u...", static_cast<unsigned>(update_node));
    request_version();
}

void handle_update_reply(uint8_t origin, uint8_t command, const uint8_t *payload, size_t length)
{
    if (origin != update_node || update_step == UpdateStep::Idle) {
        return;
    }
    if (command == RING_FIRMWARE_START && length == 3) {
        Serial.printf("Node %u firmware %u.%u, update=%u\n", static_cast<unsigned>(origin),
                      static_cast<unsigned>(payload[0]), static_cast<unsigned>(payload[1]),
                      static_cast<unsigned>(payload[2]));
        if (update_step == UpdateStep::Start) {
            if (payload[2] == 1U) {
                send_next_chunk();
            } else {
                finish_update("Node does not need an update");
            }
        } else if (update_step == UpdateStep::Verify) {
            finish_update(payload[0] == image_major && payload[1] == image_minor && payload[2] == 0
                              ? "Update verified" : "Unexpected version after reboot");
        }
    } else if (update_step == UpdateStep::Data && command == RING_FIRMWARE_DATA &&
               length == 2 && ((static_cast<size_t>(payload[0]) << 8) | payload[1]) == image_offset) {
        image_offset += chunk_size;
        send_next_chunk();
    } else if (update_step == UpdateStep::Apply && command == RING_FIRMWARE_APPLY && length == 5) {
        Serial.printf("Node %u apply CRC %08lX, accepted=%u\n", static_cast<unsigned>(origin),
                      static_cast<unsigned long>(firmware_read_be32(payload)),
                      static_cast<unsigned>(payload[4]));
        if (payload[4] != 1 || firmware_read_be32(payload) != image_crc) {
            finish_update("Node rejected staged image");
            return;
        }
        update_step = UpdateStep::BootWait;
        deadline = millis() + 2500;
        lv_label_set_text(status_label, "Installing image and rebooting...");
    } else if (update_step == UpdateStep::Address && command == RING_NODE_CONFIG && length == 1) {
        update_step = UpdateStep::Verify;
        request_version();
    }
}

void poll_update()
{
    if (update_step == UpdateStep::Idle || static_cast<int32_t>(millis() - deadline) < 0) {
        return;
    }
    if (update_step == UpdateStep::BootWait) {
        update_step = UpdateStep::Address;
        transmit_update(RING_UNASSIGNED, RING_SET_ADDRESS, &update_node, 1, RING_ASSIGNMENT_TTL);
        lv_label_set_text(status_label, "Reassigning updated node...");
    } else if ((update_step == UpdateStep::Start || update_step == UpdateStep::Data ||
                update_step == UpdateStep::Verify) && retries < 2) {
        ++retries;
        Serial1.write(outgoing, outgoing_size);
        deadline = millis() + 750;
    } else {
        finish_update("Update timed out. Check the ring.");
    }
}
#endif

void start_address_assignment(lv_event_t *)
{
#if PINBALL_HAS_UPDATE_IMAGE
    if (update_step != UpdateStep::Idle) {
        return;
    }
#endif
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
#if PINBALL_HAS_UPDATE_IMAGE
    if (update_step != UpdateStep::Idle) {
        return;
    }
#endif
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

#if PINBALL_HAS_UPDATE_IMAGE
    update_node_selector = lv_dropdown_create(screen);
    lv_dropdown_set_options(update_node_selector, "Node 1\nNode 2\nNode 3\nNode 4");
    lv_obj_set_width(update_node_selector, 100);
    lv_obj_align(update_node_selector, LV_ALIGN_TOP_LEFT, 20, 10);
    lv_obj_t *update_button = lv_btn_create(screen);
    lv_obj_set_size(update_button, 106, 36);
    lv_obj_align(update_button, LV_ALIGN_TOP_LEFT, 130, 10);
    lv_obj_add_event_cb(update_button, start_update, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *update_label = lv_label_create(update_button);
    lv_label_set_text_fmt(update_label, "Update %u.%u", static_cast<unsigned>(image_major),
                          static_cast<unsigned>(image_minor));
    lv_obj_center(update_label);
#endif

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
            } else if (dest == RING_COORDINATOR && command == RING_DEBUG &&
                       origin <= RING_LAST_UNICAST) {
                Serial.printf("Node %u debug: ", static_cast<unsigned>(origin));
                Serial.write(config_rx + 3, config_rx_size - 5);
                Serial.println();
            } else if (dest == RING_COORDINATOR && command == RING_NODE_CONFIG &&
                       origin <= RING_LAST_UNICAST && config_rx_size == RING_ADDRESS_FRAME_SIZE) {
#if PINBALL_HAS_UPDATE_IMAGE
                if (update_step == UpdateStep::Idle)
#endif
                lv_label_set_text_fmt(status_label, "Node %u assigned",
                                      static_cast<unsigned>(origin));
                Serial.printf("Node %u config: %02X\n", static_cast<unsigned>(origin),
                              static_cast<unsigned>(config_rx[3]));
            }
        }
#if PINBALL_HAS_UPDATE_IMAGE
        if (!discard_rx && origin != RING_COORDINATOR && dest == RING_COORDINATOR) {
            handle_update_reply(origin, command, config_rx + 3, config_rx_size - 5);
        }
#endif
        config_rx_size = 0;
    }
#if PINBALL_HAS_UPDATE_IMAGE
    poll_update();
#endif
}
