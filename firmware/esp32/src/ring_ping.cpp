#include <Arduino.h>
#include <lvgl.h>
#include "ring_frame.h"
#include "firmware_update.h"
#include "py32_update_image.h"

namespace {
constexpr size_t history_size = 5;
constexpr uint32_t idle_start_ms = 1000;
constexpr uint32_t idle_period_ms = 500;
uint32_t last_tx_ms;
uint32_t idle_delay_ms = idle_start_ms;
lv_obj_t *node_selector;
lv_obj_t *port_button;
lv_obj_t *port_label;
lv_obj_t *sliders[3];
lv_obj_t *slider_labels[3];
lv_obj_t *monitor_label;
lv_obj_t *port_type_selector;
lv_obj_t *servo_slider;
lv_obj_t *servo_label;
lv_obj_t *readings_label;
lv_obj_t *status_label;
uint8_t selected_port;
bool monitoring;
uint8_t monitor_node;
uint8_t monitor_port;
uint8_t readings[history_size];
size_t reading_count;

uint8_t selected_node()
{
    return static_cast<uint8_t>(lv_dropdown_get_selected(node_selector) + 1U);
}

uint8_t config_rx[RING_MAX_LENGTH + 1];
size_t config_rx_size;
uint32_t last_rx_ms;
bool discard_rx;

bool send_ring_frame(const uint8_t *frame, size_t size)
{
    if (Serial1.write(frame, size) != size) {
        return false;
    }
    last_tx_ms = millis();
    idle_delay_ms = idle_start_ms;
    return true;
}

void poll_idle()
{
    if (millis() - last_tx_ms < idle_delay_ms) {
        return;
    }
    uint8_t frame[RING_IDLE_FRAME_SIZE];
    ring_build_idle(frame);
    if (send_ring_frame(frame, sizeof frame)) {
        idle_delay_ms = idle_period_ms;
    }
}

void log_ring_tx(const uint8_t *frame, size_t size)
{
    Serial.print("Ring TX:");
    for (size_t i = 0; i < size; ++i) {
        Serial.printf(" %02X", static_cast<unsigned>(frame[i]));
    }
    Serial.println();
}

void query_port_type(lv_event_t *);

#if PINBALL_HAS_UPDATE_IMAGE
enum class UpdateStep { Idle, Start, Data, Apply, BootWait, Address, Verify };
UpdateStep update_step = UpdateStep::Idle;
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
    send_ring_frame(outgoing, outgoing_size);
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
    if (monitoring) {
        lv_label_set_text(status_label, "Stop monitoring before updating");
        return;
    }
    update_node = selected_node();
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
            const bool verified = payload[0] == image_major && payload[1] == image_minor &&
                                  payload[2] == 0;
            finish_update(verified ? "Update verified" : "Unexpected version after reboot");
            if (verified && origin == selected_node()) {
                query_port_type(nullptr);
            }
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
        send_ring_frame(outgoing, outgoing_size);
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
    if (monitoring) {
        lv_label_set_text(status_label, "Stop monitoring before assigning addresses");
        return;
    }
    uint8_t frame[RING_ADDRESS_FRAME_SIZE];
    ring_build_address_frame(frame, RING_COORDINATOR, RING_UNASSIGNED,
                             RING_SET_ADDRESS, RING_NODE_1);
    if (send_ring_frame(frame, sizeof frame)) {
        log_ring_tx(frame, sizeof frame);
        lv_label_set_text(status_label, "Address assignment sent");
    } else {
        lv_label_set_text(status_label, "UART send failed");
    }
}

void select_next_port(lv_event_t *)
{
    selected_port = (selected_port + 1U) % RING_LED_COUNT;
    lv_label_set_text_fmt(port_label, "Port %u", static_cast<unsigned>(selected_port + 1U));
    query_port_type(nullptr);
}

void refresh_slider_label(lv_event_t *event)
{
    const size_t channel = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    const char *names[] = {"R", "G", "B"};
    lv_label_set_text_fmt(slider_labels[channel], "%s  %d", names[channel],
                          static_cast<int>(lv_slider_get_value(sliders[channel])));
}

void send_color(lv_event_t *)
{
#if PINBALL_HAS_UPDATE_IMAGE
    if (update_step != UpdateStep::Idle) {
        return;
    }
#endif
    const uint8_t node = selected_node();
    const uint8_t red = static_cast<uint8_t>(lv_slider_get_value(sliders[0]));
    const uint8_t green = static_cast<uint8_t>(lv_slider_get_value(sliders[1]));
    const uint8_t blue = static_cast<uint8_t>(lv_slider_get_value(sliders[2]));
    uint8_t frame[RING_LED_FRAME_SIZE];
    ring_build_set_led(frame, node, selected_port, red, green, blue);
    if (!send_ring_frame(frame, sizeof frame)) {
        lv_label_set_text(status_label, "UART send failed");
        return;
    }
    lv_label_set_text_fmt(status_label, "Sent to node %u, LED%u: %u, %u, %u",
                          static_cast<unsigned>(node), static_cast<unsigned>(selected_port + 1U),
                          static_cast<unsigned>(red), static_cast<unsigned>(green),
                          static_cast<unsigned>(blue));
    log_ring_tx(frame, sizeof frame);
}

bool request_port_type(uint8_t type)
{
    uint8_t frame[RING_PORT_TYPE_FRAME_SIZE];
    ring_build_port_type(frame, RING_COORDINATOR, selected_node(), selected_port, type);
    if (!send_ring_frame(frame, sizeof frame)) {
        lv_label_set_text(status_label, "UART send failed");
        return false;
    }
    log_ring_tx(frame, sizeof frame);
    return true;
}

void query_port_type(lv_event_t *)
{
    lv_dropdown_set_selected(port_type_selector, RING_PORT_UNDEFINED);
#if PINBALL_HAS_UPDATE_IMAGE
    if (update_step != UpdateStep::Idle) {
        return;
    }
#endif
    request_port_type(RING_PORT_UNDEFINED);
}

void send_port_type(lv_event_t *)
{
#if PINBALL_HAS_UPDATE_IMAGE
    if (update_step != UpdateStep::Idle) {
        return;
    }
#endif
    const uint8_t type = static_cast<uint8_t>(lv_dropdown_get_selected(port_type_selector));
    if (request_port_type(type)) {
        lv_label_set_text_fmt(status_label, type == RING_PORT_UNDEFINED
            ? "Port type requested from node %u, port %u"
            : "Port type sent to node %u, port %u",
            static_cast<unsigned>(selected_node()), static_cast<unsigned>(selected_port + 1U));
    }
}

void toggle_monitor(lv_event_t *)
{
#if PINBALL_HAS_UPDATE_IMAGE
    if (update_step != UpdateStep::Idle) {
        return;
    }
#endif
    const bool start = !monitoring;
    const uint8_t node = start ? selected_node() : monitor_node;
    const uint8_t port = start ? selected_port : monitor_port;
    uint8_t frame[RING_SENSOR_FRAME_SIZE];
    ring_build_sensor_frame(frame, RING_COORDINATOR, node, port, start ? 1 : 0);
    if (!send_ring_frame(frame, sizeof frame)) {
        lv_label_set_text(status_label, "UART send failed");
        return;
    }

    log_ring_tx(frame, sizeof frame);
    monitoring = start;
    lv_label_set_text(monitor_label, start ? "Stop Monitoring" : "Start Monitoring");
    if (start) {
        monitor_node = node;
        monitor_port = port;
        reading_count = 0;
        lv_label_set_text(readings_label, "Waiting for readings...");
        lv_obj_add_state(node_selector, LV_STATE_DISABLED);
        lv_obj_add_state(port_button, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(node_selector, LV_STATE_DISABLED);
        lv_obj_clear_state(port_button, LV_STATE_DISABLED);
        if (reading_count == 0) {
            lv_label_set_text(readings_label, "No readings received");
        }
    }
}

void refresh_servo_label(lv_event_t *)
{
    lv_label_set_text_fmt(servo_label, "Servo %d",
                          static_cast<int>(lv_slider_get_value(servo_slider)));
}

void send_servo_position(lv_event_t *)
{
#if PINBALL_HAS_UPDATE_IMAGE
    if (update_step != UpdateStep::Idle) {
        return;
    }
#endif
    const uint8_t node = selected_node();
    const auto position = static_cast<int8_t>(lv_slider_get_value(servo_slider));
    uint8_t frame[RING_SERVO_POSITION_FRAME_SIZE];
    ring_build_servo_position(frame, node, selected_port, position);
    if (!send_ring_frame(frame, sizeof frame)) {
        lv_label_set_text(status_label, "UART send failed");
        return;
    }
    log_ring_tx(frame, sizeof frame);
    lv_label_set_text_fmt(status_label, "Servo position %d sent to node %u, port %u",
                          static_cast<int>(position), static_cast<unsigned>(node),
                          static_cast<unsigned>(selected_port + 1U));
}

void display_sensor_reading(uint8_t node, uint8_t port, uint8_t value)
{
    if (!monitoring || node != monitor_node || port != monitor_port) {
        return;
    }
    if (reading_count < history_size) {
        ++reading_count;
    }
    for (size_t i = reading_count - 1; i > 0; --i) {
        readings[i] = readings[i - 1];
    }
    readings[0] = value;

    char text[64] = {};
    size_t used = 0;
    for (size_t i = 0; i < reading_count; ++i) {
        used += snprintf(text + used, sizeof text - used, "%s%u",
                         i == 0 ? "" : "\n", static_cast<unsigned>(readings[i]));
    }
    lv_label_set_text(readings_label, text);
    Serial.printf("Node %u Hall port %u: %u\n", static_cast<unsigned>(node),
                  static_cast<unsigned>(port + 1U), static_cast<unsigned>(value));
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y,
                      lv_coord_t width, lv_event_cb_t callback, lv_obj_t **label_out = nullptr)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, 42);
    lv_obj_set_pos(button, x, y);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    if (label_out != nullptr) {
        *label_out = label;
    }
    return button;
}
}

void led_control_screen_begin()
{
    Serial1.begin(115200, SERIAL_8N1, 18, 17);
    last_tx_ms = millis();

    lv_obj_t *screen = lv_scr_act();
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Pinball Driver");
#if LV_FONT_MONTSERRAT_24
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
#endif
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 21);

    node_selector = lv_dropdown_create(screen);
    lv_dropdown_set_options(node_selector, "Node 1\nNode 2\nNode 3\nNode 4");
    lv_obj_set_size(node_selector, 108, 42);
    lv_obj_set_pos(node_selector, 24, 16);
    lv_obj_add_event_cb(node_selector, query_port_type, LV_EVENT_VALUE_CHANGED, nullptr);
    port_button = make_button(screen, "Port 1", 145, 16, 100, select_next_port, &port_label);
    make_button(screen, "Assign Addresses", 613, 16, 163, start_address_assignment);

#if PINBALL_HAS_UPDATE_IMAGE
    lv_obj_t *update_label;
    make_button(screen, "", 494, 16, 106, start_update, &update_label);
    lv_label_set_text_fmt(update_label, "Update %u.%u", static_cast<unsigned>(image_major),
                          static_cast<unsigned>(image_minor));
#endif

    port_type_selector = lv_dropdown_create(screen);
    lv_dropdown_set_options(port_type_selector,
                            "Undefined\nLED only\nLED + Hall sensor\nStandard servo\nServo + position feedback");
    lv_obj_set_size(port_type_selector, 229, 42);
    lv_obj_set_pos(port_type_selector, 44, 66);
    make_button(screen, "Set Port Type", 283, 66, 131, send_port_type);

    lv_obj_t *led_title = lv_label_create(screen);
    lv_label_set_text(led_title, "LED Color");
    lv_obj_set_pos(led_title, 44, 115);
    const char *labels[] = {"R  0", "G  0", "B  0"};
    const uint32_t colors[] = {0xC62828, 0x16834A, 0x2455C3};
    for (size_t i = 0; i < 3; ++i) {
        const lv_coord_t y = 143 + static_cast<lv_coord_t>(i) * 67;
        slider_labels[i] = lv_label_create(screen);
        lv_label_set_text(slider_labels[i], labels[i]);
        lv_obj_set_pos(slider_labels[i], 44, y);
        sliders[i] = lv_slider_create(screen);
        lv_slider_set_range(sliders[i], 0, 255);
        lv_obj_set_size(sliders[i], 310, 15);
        lv_obj_set_pos(sliders[i], 47, y + 26);
        lv_obj_set_style_bg_color(sliders[i], lv_color_hex(colors[i]), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sliders[i], lv_color_hex(colors[i]), LV_PART_KNOB);
        lv_obj_add_event_cb(sliders[i], refresh_slider_label, LV_EVENT_VALUE_CHANGED,
                            reinterpret_cast<void *>(i));
    }
    make_button(screen, "Set LED", 44, 351, 132, send_color);

    lv_obj_t *hall_title = lv_label_create(screen);
    lv_label_set_text(hall_title, "Hall Effect Sensor");
    lv_obj_set_pos(hall_title, 425, 104);
    make_button(screen, "Start Monitoring", 425, 142, 192, toggle_monitor, &monitor_label);

    lv_obj_t *history = lv_obj_create(screen);
    lv_obj_clear_flag(history, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(history, 325, 184);
    lv_obj_set_pos(history, 425, 199);
    lv_obj_t *history_title = lv_label_create(history);
    lv_label_set_text(history_title, "Latest readings (0-255)");
    lv_obj_align(history_title, LV_ALIGN_TOP_LEFT, 0, 0);
    readings_label = lv_label_create(history);
    lv_label_set_text(readings_label, "No readings yet");
    lv_obj_align(readings_label, LV_ALIGN_TOP_LEFT, 0, 28);

    servo_label = lv_label_create(screen);
    lv_obj_set_pos(servo_label, 44, 405);
    servo_slider = lv_slider_create(screen);
    lv_slider_set_range(servo_slider, -128, 127);
    lv_obj_set_size(servo_slider, 215, 15);
    lv_obj_set_pos(servo_slider, 153, 406);
    lv_obj_add_event_cb(servo_slider, refresh_servo_label, LV_EVENT_VALUE_CHANGED, nullptr);
    refresh_servo_label(nullptr);
    make_button(screen, "Set Servo", 425, 392, 140, send_servo_position);

    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "Ready");
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -24);
    query_port_type(nullptr);
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
            } else if (dest == RING_COORDINATOR && command == RING_SET_PORT_TYPE &&
                       config_rx_size == RING_PORT_TYPE_FRAME_SIZE &&
                       origin == selected_node() && config_rx[3] == selected_port &&
                       config_rx[4] <= RING_PORT_SERVO_FEEDBACK) {
                lv_dropdown_set_selected(port_type_selector, config_rx[4]);
            } else if (dest == RING_COORDINATOR && command == RING_MONITOR_SENSOR &&
                       config_rx_size == RING_SENSOR_FRAME_SIZE) {
                display_sensor_reading(origin, config_rx[3], config_rx[4]);
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
                {
                    lv_label_set_text_fmt(status_label, "Node %u assigned",
                                          static_cast<unsigned>(origin));
                    if (origin == selected_node()) {
                        query_port_type(nullptr);
                    }
                }
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
    poll_idle();
}
