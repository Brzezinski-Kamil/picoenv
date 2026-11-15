#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"
#include "lcd.h"
#include "button.h"
#include "crc.h"
#include "uart.h"

#define DISPLAY_TIMEOUT_MS 30000
#define STATUS_TIMEOUT_MS 4000
#define REPLY_TIMEOUT_MS 500
#define LONG_PRESS_US 700000
#define DEBOUNCE_US 30000

static bool showing_temp;
static bool showing_status;
static uint32_t temp_ms;
static uint32_t status_until_ms;

static void lcd_text(uint8_t row, const char *text)
{
    char line[17];

    snprintf(line, sizeof(line), "%-16.16s", text);
    lcd_set_cursor(row, 0);
    lcd_print(line);
}

static void lcd_blank(void)
{
    if (!showing_temp && !showing_status) {
        return;
    }

    lcd_text(0, "");
    lcd_text(1, "");
    showing_temp = false;
    showing_status = false;
}

static void lcd_show_temperature(float temperature)
{
    char value[17];

    snprintf(value, sizeof(value), "%6.2f C", temperature);
    lcd_text(0, "Temperature:");
    lcd_text(1, value);
    showing_temp = true;
    showing_status = false;
    temp_ms = to_ms_since_boot(get_absolute_time());
}

static void lcd_show_status(const char *text)
{
    lcd_text(0, text);
    lcd_text(1, "");
    showing_temp = false;
    showing_status = true;
    status_until_ms = to_ms_since_boot(get_absolute_time()) + STATUS_TIMEOUT_MS;
}

static bool wait_for(uint8_t expected, float *temperature)
{
    uint8_t type = 0;
    float value = 0.0f;

    if (!uart_listen(&type, &value, REPLY_TIMEOUT_MS, false)) {
        return false;
    }
    if (type != expected) {
        return false;
    }
    if (temperature != NULL) {
        *temperature = value;
    }
    return true;
}

static void request_with_preamble(uint8_t request)
{
    uart_send_preamble();
    sleep_ms(10);
    uart_send_empty(request);
}

static void request_temperature(void)
{
    lcd_show_status("Measuring...");

    for (int attempt = 0; attempt < 2; attempt++) {
        float temperature = 0.0f;

        request_with_preamble(GET);
        if (wait_for(TEMP, &temperature)) {
            printf("Temperature: %.2f C\n", temperature);
            lcd_show_temperature(temperature);
            return;
        }
    }

    printf("No temperature reply\n");
    lcd_show_status("No reply");
}

static void test_uart_link(void)
{
    lcd_show_status("UART test...");

    for (int attempt = 0; attempt < 2; attempt++) {
        request_with_preamble(PING);
        if (wait_for(PONG, NULL)) {
            printf("UART link OK\n");
            lcd_show_status("UART link OK");
            return;
        }
    }

    printf("UART link fail\n");
    lcd_show_status("UART link fail");
}

int main(void)
{
    stdio_init_all();

    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(4, GPIO_FUNC_I2C);
    gpio_set_function(5, GPIO_FUNC_I2C);
    gpio_pull_up(4);
    gpio_pull_up(5);

    lcd_init();
    button_init();

    if (cyw43_arch_init()) {
        return -1;
    }

    uart_setup();

    absolute_time_t next_blink = make_timeout_time_ms(500);
    bool led_on = false;
    bool pressed = false;
    absolute_time_t pressed_at = nil_time;

    while (true) {
        bool down = button_is_pressed();
        if (down && !pressed) {
            pressed = true;
            pressed_at = get_absolute_time();
        } else if (!down && pressed) {
            int64_t held_us = absolute_time_diff_us(pressed_at, get_absolute_time());
            pressed = false;
            if (held_us >= DEBOUNCE_US) {
                if (held_us >= LONG_PRESS_US) {
                    test_uart_link();
                } else {
                    request_temperature();
                }
            }
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (showing_status && (int32_t)(now - status_until_ms) >= 0) {
            lcd_blank();
        }
        if (showing_temp && (uint32_t)(now - temp_ms) > DISPLAY_TIMEOUT_MS) {
            lcd_blank();
        }

        if (absolute_time_diff_us(get_absolute_time(), next_blink) <= 0) {
            led_on = !led_on;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
            next_blink = make_timeout_time_ms(led_on ? 100 : 500);
        }

        sleep_ms(1);
    }
}
