#include "bme280.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"


static uint16_t dig_T1;
static int16_t dig_T2;
static int16_t dig_T3;
static int32_t t_fine;


#define BME280_CHIP_ID 0x60
#define BME280_CHIP_ID_REG 0xD0
#define BME280_REG_CTRL_MEAS 0xF4

/* osrs_t x1, pressure skipped, mode sleep */
#define BME280_CTRL_SLEEP 0x20
/* osrs_t x1, pressure skipped, mode forced */
#define BME280_CTRL_FORCED 0x21

static uint8_t read_u8(uint8_t reg)
{
    uint8_t data;

    i2c_write_blocking(I2C_PORT, BME280_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, BME280_ADDR, &data, 1, false);

    return data;
}

static uint16_t read_u16(uint8_t reg)
{
    uint8_t data[2];

    i2c_write_blocking(
        I2C_PORT,
        BME280_ADDR,
        &reg,
        1,
        true
    );

    i2c_read_blocking(
        I2C_PORT,
        BME280_ADDR,
        data,
        2,
        false
    );

    return (uint16_t)data[0] |
           ((uint16_t)data[1] << 8);
}


static int16_t read_s16(uint8_t reg)
{
    return (int16_t)read_u16(reg);
}


static int32_t read_raw_temperature(void)
{
    uint8_t reg = 0xFA;
    uint8_t data[3];

    i2c_write_blocking(
        I2C_PORT,
        BME280_ADDR,
        &reg,
        1,
        true
    );

    i2c_read_blocking(
        I2C_PORT,
        BME280_ADDR,
        data,
        3,
        false
    );

    return ((int32_t)data[0] << 12) |
           ((int32_t)data[1] << 4) |
           ((int32_t)data[2] >> 4);
}


static float calculate_temperature(int32_t adc_T)
{
    float var1;
    float var2;

    var1 = ((float)adc_T / 16384.0f -
            (float)dig_T1 / 1024.0f)
           * (float)dig_T2;

    var2 = ((float)adc_T / 131072.0f -
            (float)dig_T1 / 8192.0f);

    var2 = var2 * var2 *
           (float)dig_T3;

    t_fine = (int32_t)(var1 + var2);

    return (float)t_fine / 5120.0f;
}


static void i2c_bus_init(void)
{
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);
}

bool bme280_init(void)
{
    i2c_bus_init();

    if (read_u8(BME280_CHIP_ID_REG) != BME280_CHIP_ID) {
        printf("BME280 chip id mismatch\n");
        return false;
    }

    /*
     * Read temperature calibration
     */
    dig_T1 = read_u16(0x88);
    dig_T2 = read_s16(0x8A);
    dig_T3 = read_s16(0x8C);

    bme280_sleep();
    return true;
}

void bme280_sleep(void)
{
    i2c_bus_init();

    uint8_t config[] = {
        BME280_REG_CTRL_MEAS,
        BME280_CTRL_SLEEP
    };

    i2c_write_blocking(I2C_PORT, BME280_ADDR, config, 2, false);
}

float bme280_read_temperature(void)
{
    i2c_bus_init();

    uint8_t config[] = {
        BME280_REG_CTRL_MEAS,
        BME280_CTRL_FORCED
    };

    i2c_write_blocking(I2C_PORT, BME280_ADDR, config, 2, false);
    /* One x1 temperature conversion is under 2 ms. */
    sleep_ms(10);

    int32_t raw_temperature = read_raw_temperature();
    bme280_sleep();
    return calculate_temperature(raw_temperature);
}