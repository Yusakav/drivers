/**
 * @file bq25710_soft_i2c_adapter.c
 * @brief BQ25710 SMBus callback adapter for the software-I2C driver.
 */
#include "bq25710_soft_i2c_adapter.h"

#include <stdint.h>

#define BQ25710_SOFT_I2C_ADDRESS 0x09U
#define DELAY_CHUNK_MS (UINT32_MAX / 1000U)

static soft_i2c_bus_t *s_bus;
static soft_smbus_pec_t s_pec;

static int8_t adapter_read(uint8_t reg, uint16_t *value)
{
    if ((s_bus == NULL) || (value == NULL)) {
        return -1;
    }
    soft_i2c_ret_t ret = soft_smbus_read_word_data(
        s_bus, BQ25710_SOFT_I2C_ADDRESS, reg, value, s_pec);
    return (ret == SOFT_I2C_OK) ? 0 : -1;
}

static int8_t adapter_write(uint8_t reg, uint16_t value)
{
    if (s_bus == NULL) {
        return -1;
    }
    soft_i2c_ret_t ret = soft_smbus_write_word_data(
        s_bus, BQ25710_SOFT_I2C_ADDRESS, reg, value, s_pec);
    return (ret == SOFT_I2C_OK) ? 0 : -1;
}

static void adapter_delay_ms(uint32_t ms)
{
    while (ms > DELAY_CHUNK_MS) {
        s_bus->config.delay_us(s_bus->config.user, DELAY_CHUNK_MS * 1000U);
        ms -= DELAY_CHUNK_MS;
    }
    s_bus->config.delay_us(s_bus->config.user, ms * 1000U);
}

bq25710_err_t bq25710_soft_i2c_init(soft_i2c_bus_t *bus,
                                     soft_smbus_pec_t pec)
{
    bq25710_err_t ret;

    if (bus == NULL) {
        return BQ25710_ERR_NULL_PTR;
    }
    if ((bus->initialized == 0U) ||
        ((pec != SOFT_SMBUS_PEC_DISABLED) &&
         (pec != SOFT_SMBUS_PEC_ENABLED))) {
        return BQ25710_ERR_PARAM;
    }
    s_bus = bus;
    s_pec = pec;
    ret = bq25710_init(adapter_read, adapter_write, adapter_delay_ms);
    return ret;
}
