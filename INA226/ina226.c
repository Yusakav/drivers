/**
 * @file ina226.c
 * @brief Platform-independent INA226 multi-instance driver.
 */

#include "ina226.h"

#include <stddef.h>
#include <string.h>

#define INA226_CONFIG_RESET_BIT      0x8000U
#define INA226_CONFIG_RESERVED_MASK  0x7000U
#define INA226_CONFIG_RESERVED_VALUE 0x4000U
#define INA226_CONFIG_AVG_POS        9U
#define INA226_CONFIG_VBUSCT_POS     6U
#define INA226_CONFIG_VSHCT_POS      3U

#define INA226_MASK_AFF              0x0010U
#define INA226_MASK_CVRF             0x0008U
#define INA226_MASK_OVF              0x0004U
#define INA226_MASK_APOL             0x0002U
#define INA226_MASK_LEN              0x0001U
#define INA226_ALERT_FUNCTION_MASK   0xFC00U

#define INA226_SHUNT_LSB_V           0.0000025f
#define INA226_BUS_LSB_V             0.00125f
#define INA226_CALIBRATION_FACTOR    0.00512
#define INA226_CURRENT_DENOMINATOR   32768.0

static ina226_ret_t check_ready(const ina226_t *dev)
{
    if (dev == NULL) {
        return INA226_RET_NULL;
    }
    if ((dev->ops == NULL) || (dev->ops->read_reg16 == NULL) ||
        (dev->ops->write_reg16 == NULL) || (dev->initialized == 0U)) {
        return INA226_RET_PARAM;
    }
    return INA226_OK;
}

static uint8_t address_is_valid(uint8_t address)
{
    return (uint8_t)((address >= 0x40U) && (address <= 0x4FU));
}

static ina226_ret_t read_reg(ina226_t *dev, uint8_t reg, uint16_t *value)
{
    ina226_ret_t ready;

    if (value == NULL) {
        return INA226_RET_NULL;
    }

    ready = check_ready(dev);
    if (ready != INA226_OK) {
        return ready;
    }

    if (dev->ops->read_reg16(dev->user, dev->address, reg, value) != 0) {
        return INA226_RET_IO;
    }

    return INA226_OK;
}

static ina226_ret_t write_reg(ina226_t *dev, uint8_t reg, uint16_t value)
{
    ina226_ret_t ready = check_ready(dev);

    if (ready != INA226_OK) {
        return ready;
    }

    if (dev->ops->write_reg16(dev->user, dev->address, reg, value) != 0) {
        return INA226_RET_IO;
    }

    return INA226_OK;
}

ina226_ret_t ina226_init(ina226_t *dev,
                         const ina226_i2c_ops_t *ops,
                         void *user,
                         uint8_t address)
{
    ina226_device_info_t info;
    ina226_ret_t ret;

    if ((dev == NULL) || (ops == NULL)) {
        return INA226_RET_NULL;
    }
    if ((ops->read_reg16 == NULL) || (ops->write_reg16 == NULL)) {
        return INA226_RET_NULL;
    }
    if (!address_is_valid(address)) {
        return INA226_RET_RANGE;
    }

    memset(dev, 0, sizeof(*dev));
    dev->ops = ops;
    dev->user = user;
    dev->address = address;
    dev->config_value = 0x4127U;
    dev->initialized = 1U;

    ret = ina226_read_device_info(dev, &info);
    if (ret != INA226_OK) {
        memset(dev, 0, sizeof(*dev));
        return ret;
    }
    if ((info.manufacturer_id != INA226_MANUFACTURER_ID_VALUE) ||
        (info.die_id != INA226_DIE_ID_VALUE)) {
        memset(dev, 0, sizeof(*dev));
        return INA226_RET_ID;
    }

    return INA226_OK;
}

ina226_ret_t ina226_reset(ina226_t *dev)
{
    ina226_ret_t ret = write_reg(dev, INA226_REG_CONFIG, INA226_CONFIG_RESET_BIT);

    if (ret != INA226_OK) {
        return ret;
    }

    dev->config_value = 0x4127U;
    dev->current_lsb_A = 0.0f;
    dev->power_lsb_W = 0.0f;
    dev->calibration_value = 0U;
    dev->calibrated = 0U;
    return INA226_OK;
}

ina226_ret_t ina226_read_device_info(ina226_t *dev,
                                     ina226_device_info_t *info)
{
    uint16_t manufacturer;
    uint16_t die;
    ina226_ret_t ret;

    if (info == NULL) {
        return INA226_RET_NULL;
    }

    ret = read_reg(dev, INA226_REG_MANUFACTURER, &manufacturer);
    if (ret != INA226_OK) {
        return ret;
    }
    ret = read_reg(dev, INA226_REG_DIE_ID, &die);
    if (ret != INA226_OK) {
        return ret;
    }

    info->manufacturer_id = manufacturer;
    info->die_id = die;
    info->device_id = (uint16_t)(die >> 4);
    info->revision_id = (uint8_t)(die & 0x000FU);
    return INA226_OK;
}

ina226_ret_t ina226_configure(ina226_t *dev,
                              ina226_avg_t avg,
                              ina226_conv_time_t bus_ct,
                              ina226_conv_time_t shunt_ct,
                              ina226_mode_t mode)
{
    uint16_t value;
    ina226_ret_t ret;

    if ((avg > INA226_AVG_1024) || (bus_ct > INA226_CT_8244US) ||
        (shunt_ct > INA226_CT_8244US) || (mode > INA226_MODE_SHUNT_BUS_CONT)) {
        return INA226_RET_PARAM;
    }

    value = (uint16_t)(((uint16_t)avg << INA226_CONFIG_AVG_POS) |
                       ((uint16_t)bus_ct << INA226_CONFIG_VBUSCT_POS) |
                       ((uint16_t)shunt_ct << INA226_CONFIG_VSHCT_POS) |
                       ((uint16_t)mode));
    value &= (uint16_t)~INA226_CONFIG_RESERVED_MASK;
    value |= INA226_CONFIG_RESERVED_VALUE;

    ret = write_reg(dev, INA226_REG_CONFIG, value);
    if (ret != INA226_OK) {
        return ret;
    }

    dev->config_value = value;
    return INA226_OK;
}

ina226_ret_t ina226_calibrate(ina226_t *dev,
                              float r_shunt_ohm,
                              float max_expected_current_A,
                              float current_lsb_A,
                              ina226_calibration_t *cal_out)
{
    double selected_lsb;
    double calibration_f;
    uint16_t calibration;
    ina226_ret_t ret;

    if (dev == NULL) {
        return INA226_RET_NULL;
    }
    if ((r_shunt_ohm <= 0.0f) || (max_expected_current_A <= 0.0f) ||
        (current_lsb_A < 0.0f)) {
        return INA226_RET_PARAM;
    }

    selected_lsb = (double)current_lsb_A;
    if (selected_lsb == 0.0) {
        selected_lsb = (double)max_expected_current_A /
                       (double)INA226_CURRENT_DENOMINATOR;
    }
    if (selected_lsb <= 0.0) {
        return INA226_RET_PARAM;
    }

    calibration_f = (double)INA226_CALIBRATION_FACTOR /
                    (selected_lsb * (double)r_shunt_ohm);
    if ((calibration_f < 1.0f) || (calibration_f > 32767.0f)) {
        return INA226_RET_RANGE;
    }

    calibration = (uint16_t)(calibration_f + 0.01);
    if (calibration == 0U) {
        return INA226_RET_RANGE;
    }

    ret = write_reg(dev, INA226_REG_CALIBRATION, calibration);
    if (ret != INA226_OK) {
        return ret;
    }

    dev->current_lsb_A = (float)selected_lsb;
    dev->power_lsb_W = (float)(selected_lsb * 25.0);
    dev->calibration_value = calibration;
    dev->calibrated = 1U;

    if (cal_out != NULL) {
        cal_out->calibration_value = calibration;
        cal_out->current_lsb_A = dev->current_lsb_A;
        cal_out->power_lsb_W = dev->power_lsb_W;
        cal_out->r_shunt_ohm = r_shunt_ohm;
        cal_out->max_expected_current_A = max_expected_current_A;
    }

    return INA226_OK;
}

ina226_ret_t ina226_read_bus_voltage(ina226_t *dev,
                                     uint16_t *raw,
                                     float *voltage_V)
{
    uint16_t value;
    ina226_ret_t ret = read_reg(dev, INA226_REG_BUS_VOLTAGE, &value);

    if (ret != INA226_OK) {
        return ret;
    }

    value &= 0x7FFFU;
    if (raw != NULL) {
        *raw = value;
    }
    if (voltage_V != NULL) {
        *voltage_V = (float)value * INA226_BUS_LSB_V;
    }

    return INA226_OK;
}

ina226_ret_t ina226_read_shunt_voltage(ina226_t *dev,
                                       int16_t *raw,
                                       float *voltage_V)
{
    uint16_t value;
    int16_t signed_value;
    ina226_ret_t ret = read_reg(dev, INA226_REG_SHUNT_VOLTAGE, &value);

    if (ret != INA226_OK) {
        return ret;
    }

    signed_value = (int16_t)value;
    if (raw != NULL) {
        *raw = signed_value;
    }
    if (voltage_V != NULL) {
        *voltage_V = (float)signed_value * INA226_SHUNT_LSB_V;
    }

    return INA226_OK;
}

ina226_ret_t ina226_read_current(ina226_t *dev,
                                 int16_t *raw,
                                 float *current_A)
{
    uint16_t value;
    int16_t signed_value;
    ina226_ret_t ret;

    if (dev == NULL) {
        return INA226_RET_NULL;
    }
    if (dev->calibrated == 0U) {
        return INA226_RET_CALIBRATION;
    }

    ret = read_reg(dev, INA226_REG_CURRENT, &value);
    if (ret != INA226_OK) {
        return ret;
    }

    signed_value = (int16_t)value;
    if (raw != NULL) {
        *raw = signed_value;
    }
    if (current_A != NULL) {
        *current_A = (float)signed_value * dev->current_lsb_A;
    }

    return INA226_OK;
}

ina226_ret_t ina226_read_power(ina226_t *dev,
                               uint16_t *raw,
                               float *power_W)
{
    uint16_t value;
    ina226_ret_t ret;

    if (dev == NULL) {
        return INA226_RET_NULL;
    }
    if (dev->calibrated == 0U) {
        return INA226_RET_CALIBRATION;
    }

    ret = read_reg(dev, INA226_REG_POWER, &value);
    if (ret != INA226_OK) {
        return ret;
    }

    if (raw != NULL) {
        *raw = value;
    }
    if (power_W != NULL) {
        *power_W = (float)value * dev->power_lsb_W;
    }

    return INA226_OK;
}

ina226_ret_t ina226_read_all(ina226_t *dev, ina226_meas_result_t *meas)
{
    ina226_ret_t ret;

    if (meas == NULL) {
        return INA226_RET_NULL;
    }

    memset(meas, 0, sizeof(*meas));

    ret = ina226_read_shunt_voltage(dev, &meas->shunt_raw,
                                    &meas->shunt_voltage_V);
    if (ret != INA226_OK) {
        return ret;
    }
    ret = ina226_read_bus_voltage(dev, &meas->bus_raw,
                                  &meas->bus_voltage_V);
    if (ret != INA226_OK) {
        return ret;
    }
    ret = ina226_read_current(dev, &meas->current_raw, &meas->current_A);
    if (ret != INA226_OK) {
        return ret;
    }
    ret = ina226_read_power(dev, &meas->power_raw, &meas->power_W);
    if (ret != INA226_OK) {
        return ret;
    }
    ret = read_reg(dev, INA226_REG_MASK_ENABLE, &meas->mask_enable_raw);
    if (ret != INA226_OK) {
        return ret;
    }

    meas->alert_function_flag =
        (uint8_t)((meas->mask_enable_raw & INA226_MASK_AFF) != 0U);
    meas->conversion_ready =
        (uint8_t)((meas->mask_enable_raw & INA226_MASK_CVRF) != 0U);
    meas->math_overflow =
        (uint8_t)((meas->mask_enable_raw & INA226_MASK_OVF) != 0U);

    return INA226_OK;
}

ina226_ret_t ina226_configure_alert(ina226_t *dev,
                                    ina226_alert_function_t function,
                                    uint16_t limit_raw,
                                    ina226_alert_polarity_t polarity,
                                    ina226_alert_latch_t latch)
{
    uint16_t mask;
    ina226_ret_t ret;

    if (((uint16_t)function & (uint16_t)~INA226_ALERT_FUNCTION_MASK) != 0U) {
        return INA226_RET_PARAM;
    }
    if ((polarity > INA226_ALERT_POLARITY_ACTIVE_HIGH) ||
        (latch > INA226_ALERT_LATCHED)) {
        return INA226_RET_PARAM;
    }

    mask = (uint16_t)function;
    if (polarity == INA226_ALERT_POLARITY_ACTIVE_HIGH) {
        mask |= INA226_MASK_APOL;
    }
    if (latch == INA226_ALERT_LATCHED) {
        mask |= INA226_MASK_LEN;
    }

    ret = write_reg(dev, INA226_REG_ALERT_LIMIT, limit_raw);
    if (ret != INA226_OK) {
        return ret;
    }
    return write_reg(dev, INA226_REG_MASK_ENABLE, mask);
}

ina226_ret_t ina226_read_alert(ina226_t *dev,
                               ina226_alert_status_t *status)
{
    uint16_t value;
    ina226_ret_t ret;

    if (status == NULL) {
        return INA226_RET_NULL;
    }

    ret = read_reg(dev, INA226_REG_MASK_ENABLE, &value);
    if (ret != INA226_OK) {
        return ret;
    }

    status->raw = value;
    status->alert_function_flag = (uint8_t)((value & INA226_MASK_AFF) != 0U);
    status->conversion_ready = (uint8_t)((value & INA226_MASK_CVRF) != 0U);
    status->math_overflow = (uint8_t)((value & INA226_MASK_OVF) != 0U);
    status->active_high = (uint8_t)((value & INA226_MASK_APOL) != 0U);
    status->latched = (uint8_t)((value & INA226_MASK_LEN) != 0U);
    return INA226_OK;
}

ina226_ret_t ina226_dump_registers(ina226_t *dev,
                                   uint16_t regs[8])
{
    uint8_t reg;
    ina226_ret_t ret;

    if (regs == NULL) {
        return INA226_RET_NULL;
    }

    for (reg = INA226_REG_CONFIG; reg <= INA226_REG_ALERT_LIMIT; ++reg) {
        ret = read_reg(dev, reg, &regs[reg]);
        if (ret != INA226_OK) {
            return ret;
        }
    }

    return INA226_OK;
}
