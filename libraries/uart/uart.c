#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "crc.h"
#include "uart.h"

#define UART_ID uart0
#define BAUD_RATE 115200
#define PACKET_MAX_PAYLOAD 16
#define PREAMBLE_MS 20
#define IDLE_GAP_US 3000

static uint8_t rx_buf[PACKET_MAX_PAYLOAD + 7];
static uint8_t rx_index;

static void rx_reset(void)
{
    rx_index = 0;
}

static bool rx_feed(uint8_t byte, uint8_t *type, float *temperature)
{
    if (rx_index == 0 && byte != PACKET_SOF) {
        return false;
    }

    if (rx_index >= sizeof(rx_buf)) {
        rx_reset();
        if (byte != PACKET_SOF) {
            return false;
        }
    }

    rx_buf[rx_index++] = byte;

    if (rx_index < 4) {
        return false;
    }

    uint8_t payload_len = rx_buf[3];
    uint8_t total = (uint8_t)(payload_len + 7);

    if (payload_len > PACKET_MAX_PAYLOAD || total > sizeof(rx_buf)) {
        printf("Invalid packet format\n");
        rx_reset();
        return false;
    }

    if (rx_index < total) {
        return false;
    }

    bool ok = packet_decode(rx_buf, type, temperature);
    rx_reset();
    return ok;
}

static void uart_write_frame(const uint8_t *packet, uint16_t length)
{
    uart_write_blocking(UART_ID, packet, length);
    uart_tx_wait_blocking(UART_ID);
}

void uart_setup(void)
{
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_LINK_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_LINK_RX_PIN, GPIO_FUNC_UART);
    rx_reset();
}

void uart_send_temperature(float temperature)
{
    uint8_t packet[PACKET_MAX_PAYLOAD + 7];
    uint16_t length = change_temp(temperature, packet);

    uart_write_frame(packet, length);
}

void uart_send_empty(uint8_t type)
{
    uint8_t packet[7];
    uint16_t length = build_packet(VER_1, type, NULL, 0, packet);

    uart_write_frame(packet, length);
}

void uart_send_preamble(void)
{
    absolute_time_t until = make_timeout_time_ms(PREAMBLE_MS);
    uint8_t zero = 0;

    while (absolute_time_diff_us(get_absolute_time(), until) > 0) {
        uart_write_blocking(UART_ID, &zero, 1);
    }
    uart_tx_wait_blocking(UART_ID);
}

void uart_arm_rx_wake(void)
{
    uart_tx_wait_blocking(UART_ID);
    gpio_init(UART_LINK_RX_PIN);
    gpio_set_dir(UART_LINK_RX_PIN, GPIO_IN);
    gpio_pull_up(UART_LINK_RX_PIN);
}

bool uart_listen(uint8_t *type, float *temperature, uint32_t timeout_ms, bool drop_until_idle)
{
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    absolute_time_t last_byte = get_absolute_time();
    bool idle_locked = !drop_until_idle;

    rx_reset();

    while (absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        if (!uart_is_readable(UART_ID)) {
            if (drop_until_idle && !idle_locked &&
                absolute_time_diff_us(last_byte, get_absolute_time()) > IDLE_GAP_US) {
                idle_locked = true;
                rx_reset();
            }
            continue;
        }

        uint8_t byte = (uint8_t)uart_getc(UART_ID);
        last_byte = get_absolute_time();

        if (!idle_locked) {
            continue;
        }
        if (rx_feed(byte, type, temperature)) {
            return true;
        }
    }

    return false;
}
