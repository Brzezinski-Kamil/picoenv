#ifndef BME280_H
#define BME280_H

#define I2C_PORT i2c0

#define SDA_PIN 4
#define SCL_PIN 5

#define BME280_ADDR 0x76

#include <stdbool.h>
#include <stdint.h>

bool bme280_init(void);
void bme280_sleep(void);
float bme280_read_temperature(void);

#endif