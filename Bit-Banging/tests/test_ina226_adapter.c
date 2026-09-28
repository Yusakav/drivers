#include "ina226_app.h"
#include "soft_i2c.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t captured_write[3];
static size_t captured_write_length;

ina226_ret_t ina226_init(ina226_t *dev,
                         const ina226_i2c_ops_t *ops,
                         void *user,
                         uint8_t address)
{
    (void)dev; (void)ops; (void)user; (void)address;
    return INA226_OK;
}

ina226_ret_t ina226_read_device_info(ina226_t *dev, ina226_device_info_t *info)
{
    (void)dev; (void)info;
    return INA226_OK;
}

ina226_ret_t ina226_configure(ina226_t *dev,
                              ina226_avg_t avg,
                              ina226_conv_time_t bus_ct,
                              ina226_conv_time_t shunt_ct,
                              ina226_mode_t mode)
{
    (void)dev; (void)avg; (void)bus_ct; (void)shunt_ct; (void)mode;
    return INA226_OK;
}

ina226_ret_t ina226_calibrate(ina226_t *dev,
                              float r_shunt_ohm,
                              float max_expected_current_A,
                              float current_lsb_A,
                              ina226_calibration_t *cal_out)
{
    (void)dev; (void)r_shunt_ohm; (void)max_expected_current_A;
    (void)current_lsb_A; (void)cal_out;
    return INA226_OK;
}

ina226_ret_t ina226_read_all(ina226_t *dev, ina226_meas_result_t *meas)
{
    (void)dev; (void)meas;
    return INA226_OK;
}

ina226_ret_t ina226_configure_alert(ina226_t *dev,
                                    ina226_alert_function_t function,
                                    uint16_t limit_raw,
                                    ina226_alert_polarity_t polarity,
                                    ina226_alert_latch_t latch)
{
    (void)dev; (void)function; (void)limit_raw; (void)polarity; (void)latch;
    return INA226_OK;
}

ina226_ret_t ina226_dump_registers(ina226_t *dev, uint16_t regs[8])
{
    (void)dev; (void)regs;
    return INA226_OK;
}

soft_i2c_ret_t soft_i2c_transfer(soft_i2c_bus_t *bus,
                                 soft_i2c_msg_t *messages,
                                 size_t count)
{
    (void)bus;
    if (count == 2U) {
        assert(messages[0].address == 0x40U);
        assert(messages[0].flags == 0U && messages[0].length == 1U);
        assert(messages[0].buffer[0] == 0x02U);
        assert(messages[1].address == 0x40U);
        assert(messages[1].flags == SOFT_I2C_MSG_READ);
        assert(messages[1].length == 2U);
        messages[1].buffer[0] = 0x12U;
        messages[1].buffer[1] = 0x34U;
        return SOFT_I2C_OK;
    }
    assert(count == 1U);
    assert(messages[0].address == 0x40U && messages[0].flags == 0U);
    assert(messages[0].length == 3U);
    captured_write_length = messages[0].length;
    captured_write[0] = messages[0].buffer[0];
    captured_write[1] = messages[0].buffer[1];
    captured_write[2] = messages[0].buffer[2];
    return SOFT_I2C_OK;
}

int main(void)
{
    soft_i2c_bus_t bus = {0};
    uint16_t value = 0U;

    assert(ina226_soft_i2c_read_reg16(&bus, 0x40U, 0x02U, &value) == 0);
    assert(value == 0x1234U);
    assert(ina226_soft_i2c_write_reg16(&bus, 0x40U, 0x05U, 0xABCDU) == 0);
    assert(captured_write_length == 3U);
    assert(captured_write[0] == 0x05U);
    assert(captured_write[1] == 0xABU && captured_write[2] == 0xCDU);
    assert(ina226_soft_i2c_read_reg16(NULL, 0x40U, 0x02U, &value) == -1);
    return 0;
}
