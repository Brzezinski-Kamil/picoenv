#ifndef UART_H
#define UART_H

#include <stdbool.h>
#include <stdint.h>

#define UART_LINK_TX_PIN 0
#define UART_LINK_RX_PIN 1

void uart_setup(void);
void uart_send_temperature(float temperature);
void uart_send_empty(uint8_t type);
void uart_send_preamble(void);
void uart_arm_rx_wake(void);
bool uart_listen(uint8_t *type, float *temperature, uint32_t timeout_ms, bool drop_until_idle);

#endif
