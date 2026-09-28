#include "bq25710_soft_i2c_adapter.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static bq25710_smbus_read_cb_t captured_read;
static bq25710_smbus_write_cb_t captured_write;
static bq25710_delay_ms_cb_t captured_delay;
static soft_i2c_bus_t *captured_bus;
static uint8_t captured_address;
static uint8_t captured_command;
static uint16_t captured_value;
static soft_smbus_pec_t captured_pec;
static uint32_t delayed_us;

bq25710_err_t bq25710_init(bq25710_smbus_read_cb_t read_cb,
                            bq25710_smbus_write_cb_t write_cb,
                            bq25710_delay_ms_cb_t delay_cb)
{
    captured_read = read_cb;
    captured_write = write_cb;
    captured_delay = delay_cb;
    return BQ25710_ERR_OK;
}

soft_i2c_ret_t soft_smbus_read_word_data(soft_i2c_bus_t *bus,
                                         uint8_t address,
                                         uint8_t command,
                                         uint16_t *value,
                                         soft_smbus_pec_t pec)
{
    captured_bus = bus;
    captured_address = address;
    captured_command = command;
    captured_pec = pec;
    *value = 0xBEEFU;
    return SOFT_I2C_OK;
}

soft_i2c_ret_t soft_smbus_write_word_data(soft_i2c_bus_t *bus,
                                          uint8_t address,
                                          uint8_t command,
                                          uint16_t value,
                                          soft_smbus_pec_t pec)
{
    captured_bus = bus;
    captured_address = address;
    captured_command = command;
    captured_value = value;
    captured_pec = pec;
    return SOFT_I2C_OK;
}

static void mock_delay(void *user, uint32_t us)
{
    (void)user;
    delayed_us += us;
}

int main(void)
{
    soft_i2c_bus_t bus = {0};
    uint16_t value = 0U;

    bus.initialized = 1U;
    bus.config.delay_us = mock_delay;
    assert(bq25710_soft_i2c_init(NULL, SOFT_SMBUS_PEC_DISABLED) ==
           BQ25710_ERR_NULL_PTR);
    assert(bq25710_soft_i2c_init(&bus, (soft_smbus_pec_t)2) ==
           BQ25710_ERR_PARAM);
    assert(bq25710_soft_i2c_init(&bus, SOFT_SMBUS_PEC_ENABLED) ==
           BQ25710_ERR_OK);
    assert(captured_read != NULL && captured_write != NULL &&
           captured_delay != NULL);

    assert(captured_read(0x12U, &value) == 0);
    assert(value == 0xBEEFU);
    assert(captured_bus == &bus && captured_address == 0x09U);
    assert(captured_command == 0x12U && captured_pec == SOFT_SMBUS_PEC_ENABLED);

    assert(captured_write(0x34U, 0xABCDU) == 0);
    assert(captured_command == 0x34U && captured_value == 0xABCDU);
    captured_delay(5U);
    assert(delayed_us == 5000U);
    return 0;
}
