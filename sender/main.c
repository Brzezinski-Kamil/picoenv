#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/low_power.h"
#include "pico/cyw43_arch.h"
#include "bme280.h"
#include "crc.h"
#include "uart.h"

#define TEMP_ALARM_C 30.0f
#define LISTEN_MS 150

static bool radio_ready;

static void radio_off(void)
{
    if (!radio_ready) {
        return;
    }

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    cyw43_arch_deinit();
    radio_ready = false;
}

static void alarm_led(bool on)
{
    if (!on) {
        radio_off();
        return;
    }

    if (!radio_ready && cyw43_arch_init() != 0) {
        return;
    }

    radio_ready = true;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
}

static void dormant_until_rx(void)
{
    bme280_sleep();
    radio_off();
    uart_arm_rx_wake();

    int rc = low_power_dormant_until_gpio_pin_state(
        UART_LINK_RX_PIN,
        true,
        false,
        DORMANT_CLOCK_SOURCE_ROSC,
        NULL
    );
    if (rc != 0) {
        printf("Dormant failed: %d\n", rc);
        sleep_ms(100);
    }

    uart_setup();
}

int main(void)
{
    stdio_init_all();
    sleep_ms(300);

    uart_setup();

    if (!bme280_init()) {
        printf("BME280 not found\n");
        alarm_led(true);
        while (true) {
            sleep_ms(1000);
        }
    }

    while (true) {
        dormant_until_rx();

        uint8_t type = 0;
        float temperature = 0.0f;
        if (!uart_listen(&type, &temperature, LISTEN_MS, true)) {
            continue;
        }

        if (type == PING) {
            uart_send_empty(PONG);
            printf("PONG\n");
            continue;
        }

        if (type == GET) {
            temperature = bme280_read_temperature();
            uart_send_temperature(temperature);
            printf("Temperature: %.2f C\n", temperature);
            if (temperature > TEMP_ALARM_C) {
                alarm_led(true);
                sleep_ms(150);
            }
        }
    }
}
