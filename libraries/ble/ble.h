#ifndef BLE_H
#define BLE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Temperature link over BLE advertisements.
 * The payload is the same CRC frame as on UART (see libraries/crc).
 *
 * The sender broadcasts it. The receiver scans and keeps the newest valid value.
 * No connection and no pairing: both boards just have to be on.
 *
 * ble_setup() initialises the CYW43 chip. Do not also call cyw43_arch_init().
 * The onboard LED still works afterwards through cyw43_arch_gpio_put().
 *
 * The radio does not run by itself. Call ble_poll() in the main loop,
 * or ble_delay_ms() instead of sleep_ms(), otherwise advertisements stop.
 *
 * This file is not in the UART build. To switch a target over, add
 * libraries/ble/ble.c, put libraries/ble on the include path
 * (btstack_config.h has to be visible) and link:
 *   pico_btstack_ble
 *   pico_btstack_cyw43
 *   pico_cyw43_arch_none
 */

/* Development company id carried in the manufacturer field. Not registered. */
#define BLE_COMPANY_ID 0xFFFF

typedef enum {
    BLE_ROLE_SENDER,
    BLE_ROLE_RECEIVER,
} ble_role_t;

bool ble_setup(ble_role_t role);
void ble_poll(void);
void ble_delay_ms(uint32_t delay_ms);
void ble_send_temperature(float temperature);
bool ble_poll_temperature(float *temperature);

#endif
