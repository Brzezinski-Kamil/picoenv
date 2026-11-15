#include "ble.h"

#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "crc.h"
#include "pico/async_context.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#define BLE_NAME "PicoTemp"
#define TEMP_FRAME_LEN 9
#define REPORT_PERIOD_MS 1000

static ble_role_t role;
static bool stack_up;
static bool advertising;

static uint8_t adv_data[31];
static uint8_t adv_len;

static float latest_temperature;
static bool temperature_waiting;
static uint32_t last_report_ms;
static uint32_t last_crc_ms;

static btstack_packet_callback_registration_t hci_event_callback_registration;

static void build_advertisement(const uint8_t *frame, uint8_t frame_len)
{
    uint8_t index = 0;

    adv_data[index++] = 0x02;
    adv_data[index++] = BLUETOOTH_DATA_TYPE_FLAGS;
    adv_data[index++] = 0x06; // LE general discoverable, BR/EDR not supported

    adv_data[index++] = (uint8_t)(1 + sizeof(BLE_NAME) - 1);
    adv_data[index++] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&adv_data[index], BLE_NAME, sizeof(BLE_NAME) - 1);
    index = (uint8_t)(index + sizeof(BLE_NAME) - 1);

    adv_data[index++] = (uint8_t)(1 + 2 + frame_len);
    adv_data[index++] = BLUETOOTH_DATA_TYPE_MANUFACTURER_SPECIFIC_DATA;
    little_endian_store_16(adv_data, index, BLE_COMPANY_ID);
    index = (uint8_t)(index + 2);
    memcpy(&adv_data[index], frame, frame_len);
    index = (uint8_t)(index + frame_len);

    adv_len = index;
}

static void start_advertising(void)
{
    uint16_t adv_int_min = 160; // 100 ms
    uint16_t adv_int_max = 240; // 150 ms
    uint8_t adv_type = 0x03;    // ADV_NONCONN_IND
    bd_addr_t null_addr;

    memset(null_addr, 0, sizeof(null_addr));
    gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type, 0, null_addr, 0x07, 0x00);
    gap_advertisements_set_data(adv_len, adv_data);
    gap_advertisements_enable(1);
    advertising = true;
    printf("BLE advertising as %s\n", BLE_NAME);
}

/* Quiet check of one temperature frame. Kept off the scan path so a bad advert does not print. */
static bool frame_to_temperature(const uint8_t *frame, float *temperature)
{
    if (frame[0] != PACKET_SOF || frame[3] != 2 || frame[TEMP_FRAME_LEN - 1] != PACKET_EOF) {
        return false;
    }
    if (frame[1] != VER_1 || frame[2] != TEMP) {
        return false;
    }

    uint16_t crc_received = (uint16_t)frame[6] | ((uint16_t)frame[7] << 8);
    uint16_t crc_calculated = crc16_modbus(&frame[1], 5);
    if (crc_received != crc_calculated) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if ((uint32_t)(now - last_crc_ms) >= REPORT_PERIOD_MS) {
            last_crc_ms = now;
            printf("CRC error: received 0x%04X, calculated 0x%04X\n", crc_received, crc_calculated);
        }
        return false;
    }

    *temperature = bytes_to_float(frame[4], frame[5]);
    return true;
}

static void handle_advertisement(const uint8_t *data, uint8_t size, int8_t rssi)
{
    ad_context_t context;

    for (ad_iterator_init(&context, size, (uint8_t *)data); ad_iterator_has_more(&context); ad_iterator_next(&context)) {
        if (ad_iterator_get_data_type(&context) != BLUETOOTH_DATA_TYPE_MANUFACTURER_SPECIFIC_DATA) {
            continue;
        }

        uint8_t field_len = ad_iterator_get_data_len(&context);
        const uint8_t *field = ad_iterator_get_data(&context);
        if (field_len < 2 + TEMP_FRAME_LEN) {
            continue;
        }
        if (little_endian_read_16(field, 0) != BLE_COMPANY_ID) {
            continue;
        }
        if (field[2] != PACKET_SOF) {
            continue;
        }

        float temperature;
        if (!frame_to_temperature(field + 2, &temperature)) {
            continue;
        }

        latest_temperature = temperature;

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (temperature_waiting || (uint32_t)(now - last_report_ms) < REPORT_PERIOD_MS) {
            continue;
        }

        temperature_waiting = true;
        last_report_ms = now;
        printf("Received TEMP packet\n");
        printf("Temperature: %.2f°C rssi %d\n", temperature, rssi);
    }
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) {
                return;
            }

            stack_up = true;
            bd_addr_t local_addr;
            gap_local_bd_addr(local_addr);
            printf("BTstack up and running on %s.\n", bd_addr_to_str(local_addr));

            if (role == BLE_ROLE_SENDER) {
                if (adv_len > 0) {
                    start_advertising();
                }
            } else {
                // Passive scan, 100 ms interval and window.
                gap_set_scan_parameters(0, 160, 160);
                gap_start_scan();
                printf("BLE scanning\n");
            }
            break;

        case GAP_EVENT_ADVERTISING_REPORT:
            if (role != BLE_ROLE_RECEIVER) {
                break;
            }
            handle_advertisement(
                gap_event_advertising_report_get_data(packet),
                gap_event_advertising_report_get_data_length(packet),
                gap_event_advertising_report_get_rssi(packet)
            );
            break;

        default:
            break;
    }
}

bool ble_setup(ble_role_t new_role)
{
    role = new_role;

    if (cyw43_arch_init()) {
        printf("failed to initialise cyw43_arch\n");
        return false;
    }

    l2cap_init();
    sm_init();

    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    hci_power_control(HCI_POWER_ON);

    printf("BLE setup as %s\n", role == BLE_ROLE_SENDER ? "sender" : "receiver");
    return true;
}

void ble_poll(void)
{
    async_context_poll(cyw43_arch_async_context());
}

void ble_delay_ms(uint32_t delay_ms)
{
    absolute_time_t until = make_timeout_time_ms(delay_ms);

    while (absolute_time_diff_us(get_absolute_time(), until) > 0) {
        async_context_poll(cyw43_arch_async_context());
        async_context_wait_for_work_until(cyw43_arch_async_context(), until);
    }
}

void ble_send_temperature(float temperature)
{
    if (role != BLE_ROLE_SENDER) {
        printf("ble_send_temperature called on receiver\n");
        return;
    }

    uint8_t frame[16];
    uint16_t length = change_temp(temperature, frame);
    if (length != TEMP_FRAME_LEN) {
        printf("Unexpected temperature frame length %u\n", length);
        return;
    }

    build_advertisement(frame, (uint8_t)length);
    printf("BLE Temperature: %.2f C\n", temperature);

    if (!stack_up) {
        return;
    }

    gap_advertisements_set_data(adv_len, adv_data);
    if (!advertising) {
        start_advertising();
    }
}

bool ble_poll_temperature(float *temperature)
{
    if (!temperature_waiting) {
        return false;
    }

    *temperature = latest_temperature;
    temperature_waiting = false;
    return true;
}
