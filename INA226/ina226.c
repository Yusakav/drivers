/**
* @file ina226.c
* @brief Platform-independent INA226 多实例驱动实现
*
* 所属模块：INA226 电源/电流监测驱动（无平台依赖，I2C 操作通过回调注入）。
* 对应头文件：ina226.h。
*
* 本文件实现：
*   - 寄存器读写封装（read_reg / write_reg → ops->read_reg16 / write_reg16）；
*   - 设备初始化（memset → 绑定 ops → 读 Manufacturer/Die ID 校验）；
*   - CONFIG 寄存器编码（均值/转换时间/模式位域拼接 + 保留位强制 010b）；
*   - 校准公式（calibration = 0.00512 / (current_lsb × r_shunt) + 范围校验）；
*   - 电压/电流/功率 LSB 换算（bus×1.25mV, shunt×2.5μV, current×current_lsb, power×power_lsb）；
*   - Alert 配置（函数/极性/锁存 → MASK_ENABLE 位域 + ALERT_LIMIT 阈值）；
*   - read_all 批量读取 + Alert 标志位拆解。
*
* 校准公式推导：
*   INA226_CALIBRATION_FACTOR = 0.00512 (固定常量 = 内部 15bit DAC LSB × 32768)
*   calibration = 0.00512 / (current_lsb_A × r_shunt_ohm)
*   current_lsb_A = 0.00512 / (calibration × r_shunt_ohm)
*   power_lsb_W = current_lsb_A × 25（固定比例，INA226 内部功率=bus×current/25）
*/
#include "ina226.h"

#include <stddef.h>
#include <string.h>

/* ===== CONFIG 寄存器位域定义 ===== */
#define INA226_CONFIG_RESET_BIT      0x8000U   /**< bit15=RESET（写 1 触发软复位） */
#define INA226_CONFIG_RESERVED_MASK  0x7000U   /**< bit[14:12]=保留位 */
#define INA226_CONFIG_RESERVED_VALUE 0x4000U   /**< 保留位强制值 010b（手册要求） */
#define INA226_CONFIG_AVG_POS        9U        /**< 均值模式位域起始位置 */
#define INA226_CONFIG_VBUSCT_POS     6U        /**< 总线转换时间位域起始位置 */
#define INA226_CONFIG_VSHCT_POS      3U        /**< 分流转换时间位域起始位置 */

/* ===== MASK_ENABLE 寄存器位域定义 ===== */
#define INA226_MASK_AFF              0x0010U   /**< bit4=Alert Function Flag */
#define INA226_MASK_CVRF             0x0008U   /**< bit3=Conversion Ready Flag */
#define INA226_MASK_OVF              0x0004U   /**< bit2=Math Overflow Flag */
#define INA226_MASK_APOL             0x0002U   /**< bit1=Alert Polarity */
#define INA226_MASK_LEN              0x0001U   /**< bit0=Alert Latch Enable */
#define INA226_ALERT_FUNCTION_MASK   0xFC00U   /**< bit[15:10]=Alert Function 掩码 */

/* ===== LSB 常量 ===== */
#define INA226_SHUNT_LSB_V           0.0000025f    /**< 分流电压 LSB = 2.5μV/step */
#define INA226_BUS_LSB_V             0.00125f      /**< 总线电压 LSB = 1.25mV/step */
#define INA226_CALIBRATION_FACTOR    0.00512       /**< 校准公式分子常量 */
#define INA226_CURRENT_DENOMINATOR   32768.0       /**< 自动 LSB 计算分母（最大码值） */

/* ===== 内部辅助函数 ===== */

/**
 * @brief  检查设备实例是否就绪（已初始化 + ops 非 NULL）
 * @param dev  设备实例
 * @return INA226_OK / RET_NULL / RET_PARAM
 */
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

/**
 * @brief  检查 I2C 地址是否在合法范围（0x40 ~ 0x4F）
 */
static uint8_t address_is_valid(uint8_t address)
{
    return (uint8_t)((address >= 0x40U) && (address <= 0x4FU));
}

/**
 * @brief  读 16bit 寄存器封装（加参数校验 + check_ready）
 */
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

/**
 * @brief  写 16bit 寄存器封装（加 check_ready）
 */
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

/* ===== 对外接口 ===== */

/**
 * @brief  初始化 INA226 设备
 *
 * 流程：参数校验 → memset 清零 → 绑定 ops/user/address → 设置默认 config_value=0x4127 →
 *       读 Manufacturer/Die ID 校验 → ID 不匹配或 I2C 失败时 memset 回滚。
 */
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
    dev->config_value = 0x4127U;     /**< 默认 CONFIG 值：保留位 010b + AVG=1 + CT=1.1ms + 连续分流+总线 */
    dev->initialized = 1U;

    ret = ina226_read_device_info(dev, &info);
    if (ret != INA226_OK) {
        memset(dev, 0, sizeof(*dev));    /**< ID 读失败 → 回滚 */
        return ret;
    }
    if ((info.manufacturer_id != INA226_MANUFACTURER_ID_VALUE) ||
        (info.die_id != INA226_DIE_ID_VALUE)) {
        memset(dev, 0, sizeof(*dev));    /**< ID 不匹配 → 回滚 */
        return INA226_RET_ID;
    }

    return INA226_OK;
}

/**
 * @brief  软复位 INA226（写 CONFIG bit15=RESET + 重置校准标志）
 */
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

/**
 * @brief  读取芯片识别信息（Manufacturer ID + Die ID 拆解）
 *
 * Die ID 拆解：高 12bit = device_id, 低 4bit = revision_id。
 */
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

/**
 * @brief  配置 CONFIG 寄存器
 *
 * 位域拼接：avg→bit[11:9], bus_ct→bit[8:6], shunt_ct→bit[5:3], mode→bit[2:0]。
 * 保留位 bit[14:12] 先清零再 OR 0x4000（010b），符合手册要求。
 */
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
    /* 清零保留位 → 强制写 010b */
    value &= (uint16_t)~INA226_CONFIG_RESERVED_MASK;
    value |= INA226_CONFIG_RESERVED_VALUE;

    ret = write_reg(dev, INA226_REG_CONFIG, value);
    if (ret != INA226_OK) {
        return ret;
    }

    dev->config_value = value;
    return INA226_OK;
}

/**
 * @brief  校准 INA226
 *
 * 校准公式：calibration_f = 0.00512 / (selected_lsb × r_shunt_ohm)
 *   selected_lsb = current_lsb_A（>0 时）或 max_expected_current_A / 32768（自动）
 *   calibration 必须在 [1, 32767]
 *   calibration = round(calibration_f + 0.01)
 *   power_lsb_W = current_lsb_A × 25（INA226 内部固定比例）
 */
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

    /* 确定 current_lsb：优先用传入值，否则自动计算 */
    selected_lsb = (double)current_lsb_A;
    if (selected_lsb == 0.0) {
        selected_lsb = (double)max_expected_current_A /
                       (double)INA226_CURRENT_DENOMINATOR;
    }
    if (selected_lsb <= 0.0) {
        return INA226_RET_PARAM;
    }

    /* 计算校准值 */
    calibration_f = (double)INA226_CALIBRATION_FACTOR /
                    (selected_lsb * (double)r_shunt_ohm);
    if ((calibration_f < 1.0f) || (calibration_f > 32767.0f)) {
        return INA226_RET_RANGE;
    }

    /* 四舍五入（+0.01 避免浮点截断） */
    calibration = (uint16_t)(calibration_f + 0.01);
    if (calibration == 0U) {
        return INA226_RET_RANGE;
    }

    ret = write_reg(dev, INA226_REG_CALIBRATION, calibration);
    if (ret != INA226_OK) {
        return ret;
    }

    /* 缓存 LSB 到 dev（电流/功率换算依赖） */
    dev->current_lsb_A = (float)selected_lsb;
    dev->power_lsb_W = (float)(selected_lsb * 25.0);
    dev->calibration_value = calibration;
    dev->calibrated = 1U;

    /* 可选输出校准结果 */
    if (cal_out != NULL) {
        cal_out->calibration_value = calibration;
        cal_out->current_lsb_A = dev->current_lsb_A;
        cal_out->power_lsb_W = dev->power_lsb_W;
        cal_out->r_shunt_ohm = r_shunt_ohm;
        cal_out->max_expected_current_A = max_expected_current_A;
    }

    return INA226_OK;
}

/**
 * @brief  读取总线电压
 *
 * bit15 为预留位必须清零，物理量 = raw × 1.25mV。
 */
ina226_ret_t ina226_read_bus_voltage(ina226_t *dev,
                                     uint16_t *raw,
                                     float *voltage_V)
{
    uint16_t value;
    ina226_ret_t ret = read_reg(dev, INA226_REG_BUS_VOLTAGE, &value);

    if (ret != INA226_OK) {
        return ret;
    }

    value &= 0x7FFFU;    /**< 清零预留位 bit15 */
    if (raw != NULL) {
        *raw = value;
    }
    if (voltage_V != NULL) {
        *voltage_V = (float)value * INA226_BUS_LSB_V;
    }

    return INA226_OK;
}

/**
 * @brief  读取分流电压
 *
 * 原始码为 16bit 有符号，物理量 = raw × 2.5μV。
 */
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

    signed_value = (int16_t)value;    /**< 强转有符号 */
    if (raw != NULL) {
        *raw = signed_value;
    }
    if (voltage_V != NULL) {
        *voltage_V = (float)signed_value * INA226_SHUNT_LSB_V;
    }

    return INA226_OK;
}

/**
 * @brief  读取电流（必须先 calibrate）
 *
 * 物理量 = raw × current_lsb_A（由 calibrate 写入 dev）。
 * 未校准调用返回 RET_CALIBRATION。
 */
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

/**
 * @brief  读取功率（必须先 calibrate）
 *
 * 物理量 = raw × power_lsb_W（由 calibrate 写入 dev = current_lsb × 25）。
 * 未校准调用返回 RET_CALIBRATION。
 */
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

/**
 * @brief  批量读取完整测量结果
 *
 * 内部依次调 read_shunt_voltage / read_bus_voltage / read_current / read_power + 读 MASK_ENABLE。
 * 任何一步失败立即返回对应错误码。
 */
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

    /* 拆解 MASK_ENABLE 标志位 */
    meas->alert_function_flag =
        (uint8_t)((meas->mask_enable_raw & INA226_MASK_AFF) != 0U);
    meas->conversion_ready =
        (uint8_t)((meas->mask_enable_raw & INA226_MASK_CVRF) != 0U);
    meas->math_overflow =
        (uint8_t)((meas->mask_enable_raw & INA226_MASK_OVF) != 0U);

    return INA226_OK;
}

/**
 * @brief  配置 Alert 功能
 *
 * function 可多值 OR（如 SHUNT_OVERVOLTAGE | CONVERSION_READY）。
 * polarity/latch 追加到 MASK_ENABLE 低 2bit。
 * 先写 ALERT_LIMIT 阈值，再写 MASK_ENABLE。
 */
ina226_ret_t ina226_configure_alert(ina226_t *dev,
                                    ina226_alert_function_t function,
                                    uint16_t limit_raw,
                                    ina226_alert_polarity_t polarity,
                                    ina226_alert_latch_t latch)
{
    uint16_t mask;
    ina226_ret_t ret;

    /* function 只允许 bit[15:10]，高位必须为 0 */
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

/**
 * @brief  读取 Alert 状态
 *
 * 读 MASK_ENABLE 寄存器，拆解 5 个标志位。
 */
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

/**
 * @brief  转储全部 8 个寄存器（CONFIG ~ ALERT_LIMIT）
 *
 * 输出数组 regs[0..7] 对应寄存器 0x00..0x07。
 */
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
