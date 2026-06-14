/**
 * @file    bq25895.c
 * @brief   BQ25895 充电管理芯片驱动实现
 * @details 提供完整的 BQ25895 充电芯片驱动，包括：
 *          - 寄存器读写封装
 *          - 芯片初始化 (bq25895_init)
 *          - 充电参数配置 (电流/电压/终止电流/看门狗等)
 *          - 输入限流配置
 *          - 状态读取 (充电状态、VBUS 状态、故障状态)
 *          - OTG 升压模式控制
 *          - 中断/故障处理
 *          - 热调节和充电安全控制
 *          - 所有寄存器字段的 getter/setter
 * @version 1.0.0
 * @date    2026-06-13
 */

/*===========================================================================
 * INCLUDES
 *===========================================================================*/
#include "bq25895.h"
#include "bq25895_abstraction.h"
#include <stddef.h>

/*===========================================================================
 * DEFAULT CONFIGURATION MACROS
 *===========================================================================*/
#define BQ25895_DEFAULT_CHARGE_VOLTAGE_MV       4208U   /**< 默认充电电压: 4.208V */
#define BQ25895_DEFAULT_CHARGE_CURRENT_MA       2048U   /**< 默认充电电流: 2048mA */
#define BQ25895_DEFAULT_PRECHARGE_CURRENT_MA    128U    /**< 默认预充电电流: 128mA */
#define BQ25895_DEFAULT_TERM_CURRENT_MA         256U    /**< 默认终止电流: 256mA */
#define BQ25895_DEFAULT_INPUT_CURRENT_MA        500U    /**< 默认输入限流: 500mA */
#define BQ25895_DEFAULT_SYS_MIN_MV              3500U   /**< 默认系统最低电压: 3.5V */
#define BQ25895_DEFAULT_VINDPM_MV               4400U   /**< 默认输入电压限制: 4.4V */
#define BQ25895_DEFAULT_BOOST_VOLTAGE_MV        5126U   /**< 默认升压电压: 5.126V */
#define BQ25895_INIT_RETRY_MAX                  3U      /**< 初始化最大重试次数 */
#define BQ25895_I2C_TIMEOUT_MS                  100U    /**< I2C 超时 (ms) */
#define BQ25895_POR_DELAY_MS                    30U     /**< 上电复位稳定延时 (ms) */

/*===========================================================================
 * STATIC VARIABLES
 *===========================================================================*/
static bq25895_hal_ops_t  g_hal_ops;                    /**< 硬件抽象层操作集 */
static bool               g_is_initialized = false;     /**< 初始化标志 */

/*===========================================================================
 * INTERNAL HELPER MACROS
 *===========================================================================*/
#define CLAMP(val, lo, hi)  (((val) < (lo)) ? (lo) : (((val) > (hi)) ? (hi) : (val)))

/*===========================================================================
 * SECTION 1: I2C Register Read / Write
 *===========================================================================*/

/**
 * @brief   向 BQ25895 寄存器写入 1 字节
 * @param   reg_addr  寄存器地址 (0x00 ~ 0x14)
 * @param   data      待写入的数据
 * @return  BQ25895_OK 成功；BQ25895_ERR_NOT_INIT / BQ25895_ERR_I2C 失败
 */
static int32_t bq25895_reg_write(uint8_t reg_addr, uint8_t data)
{
    if (!g_is_initialized || g_hal_ops.i2c_write == NULL) {
        return BQ25895_ERR_NOT_INIT;
    }

    int32_t ret = g_hal_ops.i2c_write(BQ25895_I2C_ADDR_7BIT, reg_addr, data);
    return (ret == 0) ? BQ25895_OK : BQ25895_ERR_I2C;
}

/**
 * @brief   从 BQ25895 寄存器读取 1 字节
 * @param   reg_addr  寄存器地址 (0x00 ~ 0x14)
 * @param   data      读取数据存放指针
 * @return  BQ25895_OK 成功；BQ25895_ERR_NOT_INIT / BQ25895_ERR_I2C 失败
 */
static int32_t bq25895_reg_read(uint8_t reg_addr, uint8_t *data)
{
    if (!g_is_initialized || g_hal_ops.i2c_read == NULL) {
        return BQ25895_ERR_NOT_INIT;
    }
    if (data == NULL) {
        return BQ25895_ERR_PARAM;
    }

    int32_t ret = g_hal_ops.i2c_read(BQ25895_I2C_ADDR_7BIT, reg_addr, data);
    return (ret == 0) ? BQ25895_OK : BQ25895_ERR_I2C;
}

/**
 * @brief   读-修改-写寄存器特定位域
 * @param   reg_addr  寄存器地址
 * @param   mask      位域掩码
 * @param   value     新值 (已位移到对应位置)
 * @return  BQ25895_OK 成功；其他值失败
 */
static int32_t bq25895_reg_rmw(uint8_t reg_addr, uint8_t mask, uint8_t value)
{
    uint8_t reg_val = 0U;
    int32_t ret = bq25895_reg_read(reg_addr, &reg_val);
    if (ret != BQ25895_OK) {
        return ret;
    }

    reg_val = (reg_val & (~mask)) | (value & mask);
    return bq25895_reg_write(reg_addr, reg_val);
}

/**
 * @brief   带验证的寄存器写入: 写后回读确认
 * @param   reg_addr  寄存器地址
 * @param   mask      验证掩码
 * @param   value     期望值
 * @return  BQ25895_OK 成功；BQ25895_ERR_I2C 失败
 */
static int32_t bq25895_reg_validate(uint8_t reg_addr, uint8_t mask, uint8_t value)
{
    int32_t ret = bq25895_reg_rmw(reg_addr, mask, value);
    if (ret != BQ25895_OK) {
        return ret;
    }

    if (g_hal_ops.delay_us != NULL) {
        g_hal_ops.delay_us(100); /* 100us settle time */
    }

    uint8_t readback = 0U;
    ret = bq25895_reg_read(reg_addr, &readback);
    if (ret != BQ25895_OK) {
        return ret;
    }

    if ((readback & mask) != (value & mask)) {
        return BQ25895_ERR_I2C;
    }
    return BQ25895_OK;
}

/*===========================================================================
 * SECTION 2: Initialization
 *===========================================================================*/

/**
 * @brief   初始化 BQ25895 驱动
 * @details 注册硬件抽象层操作集，验证 I2C 通信，配置默认参数。
 * @param   hal_ops  硬件抽象层操作集指针 (必须非空)
 * @return  BQ25895_OK 成功；其他值失败
 */
int32_t bq25895_init(const bq25895_hal_ops_t *hal_ops)
{
    if (hal_ops == NULL) {
        return BQ25895_ERR_PARAM;
    }
    if (hal_ops->i2c_write == NULL || hal_ops->i2c_read == NULL) {
        return BQ25895_ERR_PARAM;
    }

    g_hal_ops = *hal_ops;
    g_is_initialized = true;

    /* 读取器件 ID 验证通信 */
    uint8_t reg14 = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG14, &reg14);
    if (ret != BQ25895_OK) {
        g_is_initialized = false;
        return BQ25895_ERR_I2C;
    }

    /* 验证器件型号 (PN = 111 = BQ25895) */
    if ((reg14 & BQ25895_REG14_PN_MSK) != BQ25895_REG14_PN_BQ25895) {
        g_is_initialized = false;
        return BQ25895_ERR_I2C;
    }

    return BQ25895_OK;
}

/**
 * @brief   反初始化 / 释放 BQ25895 驱动
 * @return  BQ25895_OK 成功
 */
int32_t bq25895_deinit(void)
{
    g_is_initialized = false;
    return BQ25895_OK;
}

/**
 * @brief   检查驱动是否已初始化
 * @return  true 已初始化；false 未初始化
 */
bool bq25895_is_initialized(void)
{
    return g_is_initialized;
}

/*===========================================================================
 * SECTION 3: REG00 - Input Source Control
 *===========================================================================*/

int32_t bq25895_set_hiz_mode(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG00,
                           BQ25895_REG00_EN_HIZ_MSK,
                           enable ? BQ25895_REG00_EN_HIZ_ENABLE << BQ25895_REG00_EN_HIZ_POS
                                  : BQ25895_REG00_EN_HIZ_DISABLE << BQ25895_REG00_EN_HIZ_POS);
}

int32_t bq25895_get_hiz_mode(bool *enable)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG00, &val);
    if (ret != BQ25895_OK) return ret;
    if (enable != NULL) *enable = ((val & BQ25895_REG00_EN_HIZ_MSK) != 0U);
    return BQ25895_OK;
}

int32_t bq25895_set_ilim_pin_enable(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG00,
                           BQ25895_REG00_EN_ILIM_MSK,
                           enable ? BQ25895_REG00_EN_ILIM_ENABLE << BQ25895_REG00_EN_ILIM_POS
                                  : BQ25895_REG00_EN_ILIM_DISABLE << BQ25895_REG00_EN_ILIM_POS);
}

int32_t bq25895_set_input_current_limit(uint16_t current_ma)
{
    if (current_ma < BQ25895_REG00_IINLIM_MIN) {
        current_ma = BQ25895_REG00_IINLIM_MIN;
    }
    if (current_ma > BQ25895_REG00_IINLIM_MAX) {
        current_ma = BQ25895_REG00_IINLIM_MAX;
    }

    uint8_t code = (uint8_t)((current_ma - (uint16_t)BQ25895_REG00_IINLIM_OFFSET)
                             / (uint16_t)BQ25895_REG00_IINLIM_STEP);
    return bq25895_reg_rmw(BQ25895_REG00,
                           BQ25895_REG00_IINLIM_MSK,
                           code << BQ25895_REG00_IINLIM_POS);
}

int32_t bq25895_get_input_current_limit(uint16_t *current_ma)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG00, &val);
    if (ret != BQ25895_OK) return ret;
    if (current_ma != NULL) {
        uint8_t code = (val & BQ25895_REG00_IINLIM_MSK) >> BQ25895_REG00_IINLIM_POS;
        *current_ma = (uint16_t)BQ25895_REG00_IINLIM_OFFSET
                      + (uint16_t)code * (uint16_t)BQ25895_REG00_IINLIM_STEP;
    }
    return BQ25895_OK;
}

/*===========================================================================
 * SECTION 4: REG01 - Boost Mode & VINDPM Offset Control
 *===========================================================================*/

int32_t bq25895_set_boost_hot_threshold(bq25895_bhot_t threshold)
{
    uint8_t val = 0U;
    switch (threshold) {
        case BQ25895_BHOT_VBHOT1:  val = BQ25895_REG01_BHOT_THR0; break;
        case BQ25895_BHOT_VBHOT0:  val = BQ25895_REG01_BHOT_THR1; break;
        case BQ25895_BHOT_VBHOT2:  val = BQ25895_REG01_BHOT_THR2; break;
        case BQ25895_BHOT_DISABLE: val = BQ25895_REG01_BHOT_DISABLE; break;
        default: return BQ25895_ERR_PARAM;
    }
    return bq25895_reg_rmw(BQ25895_REG01, BQ25895_REG01_BHOT_MSK, val);
}

int32_t bq25895_set_boost_cold_threshold(bq25895_bcold_t threshold)
{
    uint8_t val = (threshold == BQ25895_BCOLD_VBCOLD1)
                  ? (BQ25895_REG01_BCOLD_THR1 << BQ25895_REG01_BCOLD_POS)
                  : (BQ25895_REG01_BCOLD_THR0 << BQ25895_REG01_BCOLD_POS);
    return bq25895_reg_rmw(BQ25895_REG01, BQ25895_REG01_BCOLD_MSK, val);
}

int32_t bq25895_set_vindpm_offset(uint16_t offset_mv)
{
    if (offset_mv > BQ25895_REG01_VINDPM_OS_MAX) {
        offset_mv = BQ25895_REG01_VINDPM_OS_MAX;
    }
    uint8_t code = (uint8_t)((offset_mv - (uint16_t)BQ25895_REG01_VINDPM_OS_OFFSET)
                             / (uint16_t)BQ25895_REG01_VINDPM_OS_STEP);
    return bq25895_reg_rmw(BQ25895_REG01,
                           BQ25895_REG01_VINDPM_OS_MSK,
                           code << BQ25895_REG01_VINDPM_OS_POS);
}

/*===========================================================================
 * SECTION 5: REG02 - ADC / ICO / DPDM Control
 *===========================================================================*/

int32_t bq25895_adc_start_conversion(void)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG02, &val);
    if (ret != BQ25895_OK) return ret;

    uint8_t conv_rate = (val & BQ25895_REG02_CONV_RATE_MSK) >> BQ25895_REG02_CONV_RATE_POS;

    if (conv_rate == BQ25895_REG02_CONV_RATE_1S) {
        /* 连续模式已使能 */
        return BQ25895_OK;
    }

    /* 单次模式: 设置 CONV_START 启动 */
    return bq25895_reg_rmw(BQ25895_REG02,
                           BQ25895_REG02_CONV_START_MSK,
                           1U << BQ25895_REG02_CONV_START_POS);
}

int32_t bq25895_adc_set_mode(bq25895_adc_mode_t mode)
{
    uint8_t val = (mode == BQ25895_ADC_CONT_1S)
                  ? (BQ25895_REG02_CONV_RATE_1S << BQ25895_REG02_CONV_RATE_POS)
                  : (BQ25895_REG02_CONV_RATE_ONESHOT << BQ25895_REG02_CONV_RATE_POS);
    return bq25895_reg_rmw(BQ25895_REG02, BQ25895_REG02_CONV_RATE_MSK, val);
}

int32_t bq25895_set_boost_frequency(bq25895_boost_freq_t freq)
{
    uint8_t val = (freq == BQ25895_BOOST_FREQ_500KHZ)
                  ? (BQ25895_REG02_BOOST_FREQ_500KHZ << BQ25895_REG02_BOOST_FREQ_POS)
                  : (BQ25895_REG02_BOOST_FREQ_1P5MHZ << BQ25895_REG02_BOOST_FREQ_POS);
    return bq25895_reg_rmw(BQ25895_REG02, BQ25895_REG02_BOOST_FREQ_MSK, val);
}

int32_t bq25895_set_ico_enable(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG02,
                           BQ25895_REG02_ICO_EN_MSK,
                           enable ? (1U << BQ25895_REG02_ICO_EN_POS) : 0U);
}

int32_t bq25895_set_hvdcp_enable(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG02,
                           BQ25895_REG02_HVDCP_EN_MSK,
                           enable ? (1U << BQ25895_REG02_HVDCP_EN_POS) : 0U);
}

int32_t bq25895_set_maxcharge_enable(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG02,
                           BQ25895_REG02_MAXC_EN_MSK,
                           enable ? (1U << BQ25895_REG02_MAXC_EN_POS) : 0U);
}

int32_t bq25895_force_dpdm_detection(void)
{
    return bq25895_reg_rmw(BQ25895_REG02,
                           BQ25895_REG02_FORCE_DPDM_MSK,
                           1U << BQ25895_REG02_FORCE_DPDM_POS);
}

int32_t bq25895_set_auto_dpdm_enable(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG02,
                           BQ25895_REG02_AUTO_DPDM_EN_MSK,
                           enable ? (1U << BQ25895_REG02_AUTO_DPDM_EN_POS) : 0U);
}

/*===========================================================================
 * SECTION 6: REG03 - Charge / OTG / SYS Control
 *===========================================================================*/

int32_t bq25895_set_bat_load_enable(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG03,
                           BQ25895_REG03_BAT_LOADEN_MSK,
                           enable ? (1U << BQ25895_REG03_BAT_LOADEN_POS) : 0U);
}

int32_t bq25895_watchdog_reset(void)
{
    return bq25895_reg_rmw(BQ25895_REG03,
                           BQ25895_REG03_WD_RST_MSK,
                           1U << BQ25895_REG03_WD_RST_POS);
}

int32_t bq25895_set_otg_config(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG03,
                           BQ25895_REG03_OTG_CONFIG_MSK,
                           enable ? (1U << BQ25895_REG03_OTG_CONFIG_POS) : 0U);
}

int32_t bq25895_get_otg_config(bool *enable)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG03, &val);
    if (ret != BQ25895_OK) return ret;
    if (enable != NULL) *enable = ((val & BQ25895_REG03_OTG_CONFIG_MSK) != 0U);
    return BQ25895_OK;
}

int32_t bq25895_set_charge_enable(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG03,
                           BQ25895_REG03_CHG_CONFIG_MSK,
                           enable ? (1U << BQ25895_REG03_CHG_CONFIG_POS) : 0U);
}

int32_t bq25895_get_charge_enable(bool *enable)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG03, &val);
    if (ret != BQ25895_OK) return ret;
    if (enable != NULL) *enable = ((val & BQ25895_REG03_CHG_CONFIG_MSK) != 0U);
    return BQ25895_OK;
}

int32_t bq25895_set_sys_min_voltage(uint16_t voltage_mv)
{
    if (voltage_mv < 3000U) voltage_mv = 3000U;
    if (voltage_mv > 3700U) voltage_mv = 3700U;

    uint8_t code = (uint8_t)((voltage_mv - (uint16_t)BQ25895_REG03_SYS_MIN_OFFSET)
                             / (uint16_t)BQ25895_REG03_SYS_MIN_STEP);
    return bq25895_reg_rmw(BQ25895_REG03,
                           BQ25895_REG03_SYS_MIN_MSK,
                           code << BQ25895_REG03_SYS_MIN_POS);
}

int32_t bq25895_get_sys_min_voltage(uint16_t *voltage_mv)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG03, &val);
    if (ret != BQ25895_OK) return ret;
    if (voltage_mv != NULL) {
        uint8_t code = (val & BQ25895_REG03_SYS_MIN_MSK) >> BQ25895_REG03_SYS_MIN_POS;
        *voltage_mv = (uint16_t)BQ25895_REG03_SYS_MIN_OFFSET
                      + (uint16_t)code * (uint16_t)BQ25895_REG03_SYS_MIN_STEP;
    }
    return BQ25895_OK;
}

/*===========================================================================
 * SECTION 7: REG04 - Charge Current
 *===========================================================================*/

int32_t bq25895_set_pumpx_enable(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG04,
                           BQ25895_REG04_EN_PUMPX_MSK,
                           enable ? (1U << BQ25895_REG04_EN_PUMPX_POS) : 0U);
}

int32_t bq25895_set_charge_current(uint16_t current_ma)
{
    if (current_ma > BQ25895_REG04_ICHG_MAX) {
        current_ma = BQ25895_REG04_ICHG_MAX;
    }

    uint8_t code = (uint8_t)((current_ma - (uint16_t)BQ25895_REG04_ICHG_OFFSET)
                             / (uint16_t)BQ25895_REG04_ICHG_STEP);
    if (code > BQ25895_REG04_ICHG_MAX_CODE) {
        code = BQ25895_REG04_ICHG_MAX_CODE;
    }
    return bq25895_reg_rmw(BQ25895_REG04,
                           BQ25895_REG04_ICHG_MSK,
                           code << BQ25895_REG04_ICHG_POS);
}

int32_t bq25895_get_charge_current(uint16_t *current_ma)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG04, &val);
    if (ret != BQ25895_OK) return ret;
    if (current_ma != NULL) {
        uint8_t code = (val & BQ25895_REG04_ICHG_MSK) >> BQ25895_REG04_ICHG_POS;
        *current_ma = (uint16_t)BQ25895_REG04_ICHG_OFFSET
                      + (uint16_t)code * (uint16_t)BQ25895_REG04_ICHG_STEP;
    }
    return BQ25895_OK;
}

/*===========================================================================
 * SECTION 8: REG05 - Precharge & Termination
 *===========================================================================*/

int32_t bq25895_set_precharge_current(uint16_t current_ma)
{
    if (current_ma < BQ25895_REG05_IPRECHG_MIN) {
        current_ma = BQ25895_REG05_IPRECHG_MIN;
    }
    if (current_ma > BQ25895_REG05_IPRECHG_MAX) {
        current_ma = BQ25895_REG05_IPRECHG_MAX;
    }

    uint8_t code = (uint8_t)((current_ma - (uint16_t)BQ25895_REG05_IPRECHG_OFFSET)
                             / (uint16_t)BQ25895_REG05_IPRECHG_STEP);
    return bq25895_reg_rmw(BQ25895_REG05,
                           BQ25895_REG05_IPRECHG_MSK,
                           code << BQ25895_REG05_IPRECHG_POS);
}

int32_t bq25895_get_precharge_current(uint16_t *current_ma)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG05, &val);
    if (ret != BQ25895_OK) return ret;
    if (current_ma != NULL) {
        uint8_t code = (val & BQ25895_REG05_IPRECHG_MSK) >> BQ25895_REG05_IPRECHG_POS;
        *current_ma = (uint16_t)BQ25895_REG05_IPRECHG_OFFSET
                      + (uint16_t)code * (uint16_t)BQ25895_REG05_IPRECHG_STEP;
    }
    return BQ25895_OK;
}

int32_t bq25895_set_termination_current(uint16_t current_ma)
{
    if (current_ma < BQ25895_REG05_ITERM_MIN) {
        current_ma = BQ25895_REG05_ITERM_MIN;
    }
    if (current_ma > BQ25895_REG05_ITERM_MAX) {
        current_ma = BQ25895_REG05_ITERM_MAX;
    }

    uint8_t code = (uint8_t)((current_ma - (uint16_t)BQ25895_REG05_ITERM_OFFSET)
                             / (uint16_t)BQ25895_REG05_ITERM_STEP);
    return bq25895_reg_rmw(BQ25895_REG05,
                           BQ25895_REG05_ITERM_MSK,
                           code << BQ25895_REG05_ITERM_POS);
}

int32_t bq25895_get_termination_current(uint16_t *current_ma)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG05, &val);
    if (ret != BQ25895_OK) return ret;
    if (current_ma != NULL) {
        uint8_t code = (val & BQ25895_REG05_ITERM_MSK) >> BQ25895_REG05_ITERM_POS;
        *current_ma = (uint16_t)BQ25895_REG05_ITERM_OFFSET
                      + (uint16_t)code * (uint16_t)BQ25895_REG05_ITERM_STEP;
    }
    return BQ25895_OK;
}

/*===========================================================================
 * SECTION 9: REG06 - Charge Voltage & Threshold
 *===========================================================================*/

int32_t bq25895_set_charge_voltage(uint16_t voltage_mv)
{
    if (voltage_mv < BQ25895_REG06_VREG_MIN) {
        voltage_mv = BQ25895_REG06_VREG_MIN;
    }
    if (voltage_mv > BQ25895_REG06_VREG_MAX) {
        voltage_mv = BQ25895_REG06_VREG_MAX;
    }

    uint8_t code = (uint8_t)((voltage_mv - (uint16_t)BQ25895_REG06_VREG_OFFSET)
                             / (uint16_t)BQ25895_REG06_VREG_STEP);
    return bq25895_reg_rmw(BQ25895_REG06,
                           BQ25895_REG06_VREG_MSK,
                           code << BQ25895_REG06_VREG_POS);
}

int32_t bq25895_get_charge_voltage(uint16_t *voltage_mv)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG06, &val);
    if (ret != BQ25895_OK) return ret;
    if (voltage_mv != NULL) {
        uint8_t code = (val & BQ25895_REG06_VREG_MSK) >> BQ25895_REG06_VREG_POS;
        *voltage_mv = (uint16_t)BQ25895_REG06_VREG_OFFSET
                      + (uint16_t)code * (uint16_t)BQ25895_REG06_VREG_STEP;
    }
    return BQ25895_OK;
}

int32_t bq25895_set_batlowv_threshold(bq25895_batlowv_t threshold)
{
    uint8_t val = (threshold == BQ25895_BATLOWV_3V0)
                  ? (1U << BQ25895_REG06_BATLOWV_POS) : 0U;
    return bq25895_reg_rmw(BQ25895_REG06, BQ25895_REG06_BATLOWV_MSK, val);
}

int32_t bq25895_set_recharge_threshold(bq25895_vrechg_t threshold)
{
    uint8_t val = (threshold == BQ25895_VRECHG_200MV)
                  ? (1U << BQ25895_REG06_VRECHG_POS) : 0U;
    return bq25895_reg_rmw(BQ25895_REG06, BQ25895_REG06_VRECHG_MSK, val);
}

/*===========================================================================
 * SECTION 10: REG07 - Timer & Watchdog
 *===========================================================================*/

int32_t bq25895_set_termination_enable(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG07,
                           BQ25895_REG07_EN_TERM_MSK,
                           enable ? (1U << BQ25895_REG07_EN_TERM_POS) : 0U);
}

int32_t bq25895_set_stat_pin_disable(bool disable)
{
    return bq25895_reg_rmw(BQ25895_REG07,
                           BQ25895_REG07_STAT_DIS_MSK,
                           disable ? (1U << BQ25895_REG07_STAT_DIS_POS) : 0U);
}

int32_t bq25895_set_watchdog(bq25895_watchdog_t wdt)
{
    uint8_t val = 0U;
    switch (wdt) {
        case BQ25895_WDT_DISABLE: val = BQ25895_REG07_WATCHDOG_DISABLE; break;
        case BQ25895_WDT_40S:     val = BQ25895_REG07_WATCHDOG_40S;     break;
        case BQ25895_WDT_80S:     val = BQ25895_REG07_WATCHDOG_80S;     break;
        case BQ25895_WDT_160S:    val = BQ25895_REG07_WATCHDOG_160S;    break;
        default: return BQ25895_ERR_PARAM;
    }
    return bq25895_reg_rmw(BQ25895_REG07, BQ25895_REG07_WATCHDOG_MSK, val);
}

int32_t bq25895_set_safety_timer_enable(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG07,
                           BQ25895_REG07_EN_TIMER_MSK,
                           enable ? (1U << BQ25895_REG07_EN_TIMER_POS) : 0U);
}

int32_t bq25895_set_charge_timer(bq25895_chg_timer_t timer)
{
    uint8_t val = 0U;
    switch (timer) {
        case BQ25895_CHG_TIMER_5HRS:  val = BQ25895_REG07_CHG_TIMER_5HRS;  break;
        case BQ25895_CHG_TIMER_8HRS:  val = BQ25895_REG07_CHG_TIMER_8HRS;  break;
        case BQ25895_CHG_TIMER_12HRS: val = BQ25895_REG07_CHG_TIMER_12HRS; break;
        case BQ25895_CHG_TIMER_20HRS: val = BQ25895_REG07_CHG_TIMER_20HRS; break;
        default: return BQ25895_ERR_PARAM;
    }
    return bq25895_reg_rmw(BQ25895_REG07, BQ25895_REG07_CHG_TIMER_MSK, val);
}

/*===========================================================================
 * SECTION 11: REG08 - IRCOMP & Thermal Regulation
 *===========================================================================*/

int32_t bq25895_set_ircomp_resistance(uint8_t resistance_mohm)
{
    if (resistance_mohm > BQ25895_REG08_BAT_COMP_MAX) {
        resistance_mohm = BQ25895_REG08_BAT_COMP_MAX;
    }
    uint8_t code = (resistance_mohm - (uint8_t)BQ25895_REG08_BAT_COMP_OFFSET)
                   / (uint8_t)BQ25895_REG08_BAT_COMP_STEP;
    return bq25895_reg_rmw(BQ25895_REG08,
                           BQ25895_REG08_BAT_COMP_MSK,
                           code << BQ25895_REG08_BAT_COMP_POS);
}

int32_t bq25895_set_ircomp_clamp_voltage(uint8_t clamp_mv)
{
    if (clamp_mv > BQ25895_REG08_VCLAMP_MAX) {
        clamp_mv = BQ25895_REG08_VCLAMP_MAX;
    }
    uint8_t code = (clamp_mv - (uint8_t)BQ25895_REG08_VCLAMP_OFFSET)
                   / (uint8_t)BQ25895_REG08_VCLAMP_STEP;
    return bq25895_reg_rmw(BQ25895_REG08,
                           BQ25895_REG08_VCLAMP_MSK,
                           code << BQ25895_REG08_VCLAMP_POS);
}

int32_t bq25895_set_thermal_regulation(bq25895_treg_t threshold)
{
    uint8_t val = 0U;
    switch (threshold) {
        case BQ25895_TREG_60C:  val = BQ25895_REG08_TREG_60C;  break;
        case BQ25895_TREG_80C:  val = BQ25895_REG08_TREG_80C;  break;
        case BQ25895_TREG_100C: val = BQ25895_REG08_TREG_100C; break;
        case BQ25895_TREG_120C: val = BQ25895_REG08_TREG_120C; break;
        default: return BQ25895_ERR_PARAM;
    }
    return bq25895_reg_rmw(BQ25895_REG08, BQ25895_REG08_TREG_MSK, val);
}

/*===========================================================================
 * SECTION 12: REG09 - ICO / BATFET / PumpX Control
 *===========================================================================*/

int32_t bq25895_force_ico(void)
{
    return bq25895_reg_rmw(BQ25895_REG09,
                           BQ25895_REG09_FORCE_ICO_MSK,
                           1U << BQ25895_REG09_FORCE_ICO_POS);
}

int32_t bq25895_set_timer_2x_enable(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG09,
                           BQ25895_REG09_TMR2X_EN_MSK,
                           enable ? (1U << BQ25895_REG09_TMR2X_EN_POS) : 0U);
}

int32_t bq25895_set_batfet_disable(bool disable)
{
    return bq25895_reg_rmw(BQ25895_REG09,
                           BQ25895_REG09_BATFET_DIS_MSK,
                           disable ? (1U << BQ25895_REG09_BATFET_DIS_POS) : 0U);
}

int32_t bq25895_get_batfet_status(bool *disabled)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG09, &val);
    if (ret != BQ25895_OK) return ret;
    if (disabled != NULL) *disabled = ((val & BQ25895_REG09_BATFET_DIS_MSK) != 0U);
    return BQ25895_OK;
}

int32_t bq25895_set_batfet_delay(bq25895_batfet_dly_t delay)
{
    uint8_t val = (delay == BQ25895_BATFET_DLY_10S)
                  ? (1U << BQ25895_REG09_BATFET_DLY_POS) : 0U;
    return bq25895_reg_rmw(BQ25895_REG09, BQ25895_REG09_BATFET_DLY_MSK, val);
}

int32_t bq25895_set_batfet_reset_enable(bool enable)
{
    return bq25895_reg_rmw(BQ25895_REG09,
                           BQ25895_REG09_BATFET_RST_EN_MSK,
                           enable ? (1U << BQ25895_REG09_BATFET_RST_EN_POS) : 0U);
}

int32_t bq25895_pumpx_voltage_up(void)
{
    return bq25895_reg_rmw(BQ25895_REG09,
                           BQ25895_REG09_PUMPX_UP_MSK,
                           1U << BQ25895_REG09_PUMPX_UP_POS);
}

int32_t bq25895_pumpx_voltage_down(void)
{
    return bq25895_reg_rmw(BQ25895_REG09,
                           BQ25895_REG09_PUMPX_DN_MSK,
                           1U << BQ25895_REG09_PUMPX_DN_POS);
}

/*===========================================================================
 * SECTION 13: REG0A - Boost Voltage Control
 *===========================================================================*/

int32_t bq25895_set_boost_voltage(uint16_t voltage_mv)
{
    if (voltage_mv < BQ25895_REG0A_BOOSTV_MIN) {
        voltage_mv = BQ25895_REG0A_BOOSTV_MIN;
    }
    if (voltage_mv > BQ25895_REG0A_BOOSTV_MAX) {
        voltage_mv = BQ25895_REG0A_BOOSTV_MAX;
    }

    uint8_t code = (uint8_t)((voltage_mv - (uint16_t)BQ25895_REG0A_BOOSTV_OFFSET)
                             / (uint16_t)BQ25895_REG0A_BOOSTV_STEP);
    return bq25895_reg_rmw(BQ25895_REG0A,
                           BQ25895_REG0A_BOOSTV_MSK,
                           code << BQ25895_REG0A_BOOSTV_POS);
}

int32_t bq25895_get_boost_voltage(uint16_t *voltage_mv)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG0A, &val);
    if (ret != BQ25895_OK) return ret;
    if (voltage_mv != NULL) {
        uint8_t code = (val & BQ25895_REG0A_BOOSTV_MSK) >> BQ25895_REG0A_BOOSTV_POS;
        *voltage_mv = (uint16_t)BQ25895_REG0A_BOOSTV_OFFSET
                      + (uint16_t)code * (uint16_t)BQ25895_REG0A_BOOSTV_STEP;
    }
    return BQ25895_OK;
}

/*===========================================================================
 * SECTION 14: REG0B - Status Register (Read-Only)
 *===========================================================================*/

int32_t bq25895_get_vbus_status(bq25895_vbus_stat_t *vbus_stat)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG0B, &val);
    if (ret != BQ25895_OK) return ret;
    if (vbus_stat != NULL) {
        *vbus_stat = (bq25895_vbus_stat_t)((val & BQ25895_REG0B_VBUS_STAT_MSK)
                                            >> BQ25895_REG0B_VBUS_STAT_POS);
    }
    return BQ25895_OK;
}

int32_t bq25895_get_charge_status(bq25895_chrg_stat_t *chrg_stat)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG0B, &val);
    if (ret != BQ25895_OK) return ret;
    if (chrg_stat != NULL) {
        *chrg_stat = (bq25895_chrg_stat_t)((val & BQ25895_REG0B_CHRG_STAT_MSK)
                                            >> BQ25895_REG0B_CHRG_STAT_POS);
    }
    return BQ25895_OK;
}

int32_t bq25895_get_power_good(bool *pg)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG0B, &val);
    if (ret != BQ25895_OK) return ret;
    if (pg != NULL) *pg = ((val & BQ25895_REG0B_PG_STAT_MSK) != 0U);
    return BQ25895_OK;
}

int32_t bq25895_get_sdp_status(bool *is_usb500)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG0B, &val);
    if (ret != BQ25895_OK) return ret;
    if (is_usb500 != NULL) *is_usb500 = ((val & BQ25895_REG0B_SDP_STAT_MSK) != 0U);
    return BQ25895_OK;
}

int32_t bq25895_get_vsys_status(bool *in_vsys_reg)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG0B, &val);
    if (ret != BQ25895_OK) return ret;
    if (in_vsys_reg != NULL) *in_vsys_reg = ((val & BQ25895_REG0B_VSYS_STAT_MSK) != 0U);
    return BQ25895_OK;
}

int32_t bq25895_get_full_status(bq25895_status_t *status)
{
    if (status == NULL) return BQ25895_ERR_PARAM;
    uint8_t reg0b = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG0B, &reg0b);
    if (ret != BQ25895_OK) return ret;

    status->vbus_stat  = (bq25895_vbus_stat_t)((reg0b & BQ25895_REG0B_VBUS_STAT_MSK)
                                                >> BQ25895_REG0B_VBUS_STAT_POS);
    status->chrg_stat  = (bq25895_chrg_stat_t)((reg0b & BQ25895_REG0B_CHRG_STAT_MSK)
                                                >> BQ25895_REG0B_CHRG_STAT_POS);
    status->pg_stat    = ((reg0b & BQ25895_REG0B_PG_STAT_MSK) != 0U);
    status->sdp_stat   = ((reg0b & BQ25895_REG0B_SDP_STAT_MSK) != 0U);
    status->vsys_stat  = ((reg0b & BQ25895_REG0B_VSYS_STAT_MSK) != 0U);

    /* 读取 REG13 DPM 状态 */
    uint8_t reg13 = 0U;
    ret = bq25895_reg_read(BQ25895_REG13, &reg13);
    if (ret != BQ25895_OK) return ret;

    status->vdpm_stat = ((reg13 & BQ25895_REG13_VDPM_STAT_MSK) != 0U);
    status->idpm_stat = ((reg13 & BQ25895_REG13_IDPM_STAT_MSK) != 0U);

    uint8_t idpm_code = (reg13 & BQ25895_REG13_IDPM_LIM_MSK) >> BQ25895_REG13_IDPM_LIM_POS;
    status->idpm_lim_ma = (uint16_t)BQ25895_REG13_IDPM_LIM_OFFSET
                          + (uint16_t)idpm_code * (uint16_t)BQ25895_REG13_IDPM_LIM_STEP;

    return BQ25895_OK;
}

/*===========================================================================
 * SECTION 15: REG0C - Fault Register (Read-Only)
 *===========================================================================*/

int32_t bq25895_get_fault(bq25895_fault_t *fault)
{
    if (fault == NULL) return BQ25895_ERR_PARAM;

    /* 第一次读取: 清除历史故障锁存 */
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG0C, &val);
    if (ret != BQ25895_OK) return ret;

    /* 第二次读取: 获取当前真实故障状态 */
    ret = bq25895_reg_read(BQ25895_REG0C, &val);
    if (ret != BQ25895_OK) return ret;

    fault->watchdog_fault = ((val & BQ25895_REG0C_WATCHDOG_FAULT_MSK) != 0U);
    fault->boost_fault    = ((val & BQ25895_REG0C_BOOST_FAULT_MSK) != 0U);
    fault->chrg_fault     = (bq25895_chrg_fault_t)((val & BQ25895_REG0C_CHRG_FAULT_MSK)
                                                    >> BQ25895_REG0C_CHRG_FAULT_POS);
    fault->bat_fault      = ((val & BQ25895_REG0C_BAT_FAULT_MSK) != 0U);
    fault->ntc_fault      = (bq25895_ntc_fault_t)((val & BQ25895_REG0C_NTC_FAULT_MSK)
                                                   >> BQ25895_REG0C_NTC_FAULT_POS);
    return BQ25895_OK;
}

int32_t bq25895_clear_faults(void)
{
    /* 读取故障寄存器两次以清除锁存 */
    uint8_t dummy = 0U;
    bq25895_reg_read(BQ25895_REG0C, &dummy);
    return bq25895_reg_read(BQ25895_REG0C, &dummy);
}

/*===========================================================================
 * SECTION 16: REG0D - VINDPM Threshold
 *===========================================================================*/

int32_t bq25895_set_vindpm_absolute(uint16_t voltage_mv)
{
    /* 先启用绝对 VINDPM 模式 */
    int32_t ret = bq25895_reg_rmw(BQ25895_REG0D,
                                   BQ25895_REG0D_FORCE_VINDPM_MSK,
                                   1U << BQ25895_REG0D_FORCE_VINDPM_POS);
    if (ret != BQ25895_OK) return ret;

    if (voltage_mv < BQ25895_REG0D_VINDPM_MIN) {
        voltage_mv = BQ25895_REG0D_VINDPM_MIN;
    }
    if (voltage_mv > BQ25895_REG0D_VINDPM_MAX) {
        voltage_mv = BQ25895_REG0D_VINDPM_MAX;
    }

    uint8_t code = (uint8_t)((voltage_mv - (uint16_t)BQ25895_REG0D_VINDPM_OFFSET)
                             / (uint16_t)BQ25895_REG0D_VINDPM_STEP);
    return bq25895_reg_rmw(BQ25895_REG0D,
                           BQ25895_REG0D_VINDPM_MSK,
                           code << BQ25895_REG0D_VINDPM_POS);
}

int32_t bq25895_set_vindpm_relative(void)
{
    return bq25895_reg_rmw(BQ25895_REG0D,
                           BQ25895_REG0D_FORCE_VINDPM_MSK,
                           BQ25895_REG0D_FORCE_VINDPM_REL << BQ25895_REG0D_FORCE_VINDPM_POS);
}

int32_t bq25895_get_vindpm_threshold(uint16_t *voltage_mv)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG0D, &val);
    if (ret != BQ25895_OK) return ret;
    if (voltage_mv != NULL) {
        uint8_t code = (val & BQ25895_REG0D_VINDPM_MSK) >> BQ25895_REG0D_VINDPM_POS;
        *voltage_mv = (uint16_t)BQ25895_REG0D_VINDPM_OFFSET
                      + (uint16_t)code * (uint16_t)BQ25895_REG0D_VINDPM_STEP;
    }
    return BQ25895_OK;
}

/*===========================================================================
 * SECTION 17: REG0E ~ REG12 - ADC Read
 *===========================================================================*/

int32_t bq25895_get_battery_voltage(uint16_t *voltage_mv)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG0E, &val);
    if (ret != BQ25895_OK) return ret;
    if (voltage_mv != NULL) {
        uint8_t code = (val & BQ25895_REG0E_BATV_MSK) >> BQ25895_REG0E_BATV_POS;
        *voltage_mv = (uint16_t)BQ25895_REG0E_BATV_OFFSET
                      + (uint16_t)code * (uint16_t)BQ25895_REG0E_BATV_STEP;
    }
    return BQ25895_OK;
}

int32_t bq25895_get_system_voltage(uint16_t *voltage_mv)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG0F, &val);
    if (ret != BQ25895_OK) return ret;
    if (voltage_mv != NULL) {
        uint8_t code = (val & BQ25895_REG0F_SYSV_MSK) >> BQ25895_REG0F_SYSV_POS;
        *voltage_mv = (uint16_t)BQ25895_REG0F_SYSV_OFFSET
                      + (uint16_t)code * (uint16_t)BQ25895_REG0F_SYSV_STEP;
    }
    return BQ25895_OK;
}

int32_t bq25895_get_ts_percentage(uint16_t *ts_pct)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG10, &val);
    if (ret != BQ25895_OK) return ret;
    if (ts_pct != NULL) {
        uint8_t code = (val & BQ25895_REG10_TSPCT_MSK) >> BQ25895_REG10_TSPCT_POS;
        /* 返回 x100 的百分比值 (如 4250 = 42.50%) */
        *ts_pct = (uint16_t)BQ25895_REG10_TSPCT_OFFSET
                  + (uint16_t)code * (uint16_t)BQ25895_REG10_TSPCT_STEP;
    }
    return BQ25895_OK;
}

int32_t bq25895_get_vbus_voltage(uint16_t *voltage_mv, bool *vbus_good)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG11, &val);
    if (ret != BQ25895_OK) return ret;

    if (voltage_mv != NULL) {
        uint8_t code = (val & BQ25895_REG11_VBUSV_MSK) >> BQ25895_REG11_VBUSV_POS;
        *voltage_mv = (uint16_t)BQ25895_REG11_VBUSV_OFFSET
                      + (uint16_t)code * (uint16_t)BQ25895_REG11_VBUSV_STEP;
    }
    if (vbus_good != NULL) {
        *vbus_good = ((val & BQ25895_REG11_VBUS_GD_MSK) != 0U);
    }
    return BQ25895_OK;
}

int32_t bq25895_get_charge_current_adc(uint16_t *current_ma)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG12, &val);
    if (ret != BQ25895_OK) return ret;
    if (current_ma != NULL) {
        uint8_t code = (val & BQ25895_REG12_ICHGR_MSK) >> BQ25895_REG12_ICHGR_POS;
        *current_ma = (uint16_t)BQ25895_REG12_ICHGR_OFFSET
                      + (uint16_t)code * (uint16_t)BQ25895_REG12_ICHGR_STEP;
    }
    return BQ25895_OK;
}

int32_t bq25895_get_adc_all(bq25895_adc_result_t *adc)
{
    if (adc == NULL) return BQ25895_ERR_PARAM;

    uint8_t reg0e = 0U, reg0f = 0U, reg10 = 0U, reg11 = 0U, reg12 = 0U;
    int32_t ret;

    ret = bq25895_reg_read(BQ25895_REG0E, &reg0e);
    if (ret != BQ25895_OK) return ret;
    ret = bq25895_reg_read(BQ25895_REG0F, &reg0f);
    if (ret != BQ25895_OK) return ret;
    ret = bq25895_reg_read(BQ25895_REG10, &reg10);
    if (ret != BQ25895_OK) return ret;
    ret = bq25895_reg_read(BQ25895_REG11, &reg11);
    if (ret != BQ25895_OK) return ret;
    ret = bq25895_reg_read(BQ25895_REG12, &reg12);
    if (ret != BQ25895_OK) return ret;

    adc->therm_stat = ((reg0e & BQ25895_REG0E_THERM_STAT_MSK) != 0U);

    {
        uint8_t code = (reg0e & BQ25895_REG0E_BATV_MSK) >> BQ25895_REG0E_BATV_POS;
        adc->vbat_mv = (uint16_t)BQ25895_REG0E_BATV_OFFSET
                       + (uint16_t)code * (uint16_t)BQ25895_REG0E_BATV_STEP;
    }
    {
        uint8_t code = (reg0f & BQ25895_REG0F_SYSV_MSK) >> BQ25895_REG0F_SYSV_POS;
        adc->vsys_mv = (uint16_t)BQ25895_REG0F_SYSV_OFFSET
                        + (uint16_t)code * (uint16_t)BQ25895_REG0F_SYSV_STEP;
    }
    {
        uint8_t code = (reg10 & BQ25895_REG10_TSPCT_MSK) >> BQ25895_REG10_TSPCT_POS;
        adc->ts_pct = (uint16_t)BQ25895_REG10_TSPCT_OFFSET
                      + (uint16_t)code * (uint16_t)BQ25895_REG10_TSPCT_STEP;
    }
    {
        uint8_t code = (reg11 & BQ25895_REG11_VBUSV_MSK) >> BQ25895_REG11_VBUSV_POS;
        adc->vbus_mv = (uint16_t)BQ25895_REG11_VBUSV_OFFSET
                        + (uint16_t)code * (uint16_t)BQ25895_REG11_VBUSV_STEP;
        adc->vbus_good = ((reg11 & BQ25895_REG11_VBUS_GD_MSK) != 0U);
    }
    {
        uint8_t code = (reg12 & BQ25895_REG12_ICHGR_MSK) >> BQ25895_REG12_ICHGR_POS;
        adc->ichg_ma = (uint16_t)BQ25895_REG12_ICHGR_OFFSET
                        + (uint16_t)code * (uint16_t)BQ25895_REG12_ICHGR_STEP;
    }

    return BQ25895_OK;
}

/*===========================================================================
 * SECTION 18: REG14 - Device Info & Reset
 *===========================================================================*/

int32_t bq25895_reset_registers(void)
{
    return bq25895_reg_rmw(BQ25895_REG14,
                           BQ25895_REG14_REG_RST_MSK,
                           1U << BQ25895_REG14_REG_RST_POS);
}

int32_t bq25895_get_ico_optimized_status(bool *optimized)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG14, &val);
    if (ret != BQ25895_OK) return ret;
    if (optimized != NULL) {
        *optimized = ((val & BQ25895_REG14_ICO_OPTIMIZED_MSK) != 0U);
    }
    return BQ25895_OK;
}

int32_t bq25895_get_device_info(bq25895_device_info_t *info)
{
    if (info == NULL) return BQ25895_ERR_PARAM;
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG14, &val);
    if (ret != BQ25895_OK) return ret;

    info->part_number   = (val & BQ25895_REG14_PN_MSK) >> BQ25895_REG14_PN_POS;
    info->ts_profile    = ((val & BQ25895_REG14_TS_PROFILE_MSK) != 0U);
    info->dev_revision  = (val & BQ25895_REG14_DEV_REV_MSK) >> BQ25895_REG14_DEV_REV_POS;
    info->ico_optimized = ((val & BQ25895_REG14_ICO_OPTIMIZED_MSK) != 0U);
    return BQ25895_OK;
}

/*===========================================================================
 * SECTION 19: DPM Status (REG13)
 *===========================================================================*/

int32_t bq25895_get_dpm_status(bool *vdpm, bool *idpm, uint16_t *idpm_lim_ma)
{
    uint8_t val = 0U;
    int32_t ret = bq25895_reg_read(BQ25895_REG13, &val);
    if (ret != BQ25895_OK) return ret;

    if (vdpm != NULL) *vdpm = ((val & BQ25895_REG13_VDPM_STAT_MSK) != 0U);
    if (idpm != NULL) *idpm = ((val & BQ25895_REG13_IDPM_STAT_MSK) != 0U);
    if (idpm_lim_ma != NULL) {
        uint8_t code = (val & BQ25895_REG13_IDPM_LIM_MSK) >> BQ25895_REG13_IDPM_LIM_POS;
        *idpm_lim_ma = (uint16_t)BQ25895_REG13_IDPM_LIM_OFFSET
                        + (uint16_t)code * (uint16_t)BQ25895_REG13_IDPM_LIM_STEP;
    }
    return BQ25895_OK;
}

/*===========================================================================
 * SECTION 20: High-Level Compound API
 *===========================================================================*/

/**
 * @brief   一键配置充电参数 (电流/电压/预充电/终止)
 * @param   charge_voltage_mv  充电电压 (mV). 范围: 3840 ~ 4608
 * @param   charge_current_ma  充电电流 (mA). 范围: 0 ~ 5056
 * @param   precharge_current_ma 预充电电流 (mA). 范围: 64 ~ 1024
 * @param   term_current_ma    终止电流 (mA). 范围: 64 ~ 1024
 * @return  BQ25895_OK 成功；其他值失败
 */
int32_t bq25895_configure_charge_params(uint16_t charge_voltage_mv,
                                         uint16_t charge_current_ma,
                                         uint16_t precharge_current_ma,
                                         uint16_t term_current_ma)
{
    int32_t ret;

    ret = bq25895_set_charge_voltage(charge_voltage_mv);
    if (ret != BQ25895_OK) return ret;

    ret = bq25895_set_charge_current(charge_current_ma);
    if (ret != BQ25895_OK) return ret;

    ret = bq25895_set_precharge_current(precharge_current_ma);
    if (ret != BQ25895_OK) return ret;

    ret = bq25895_set_termination_current(term_current_ma);
    if (ret != BQ25895_OK) return ret;

    return BQ25895_OK;
}

/**
 * @brief   进入运输模式 (Ship Mode)
 * @details 关断 BATFET 切断电池供电，最小化静态功耗 (低至 12 µA)。
 *          使用 10s 延时以便安全进入运输模式。
 * @return  BQ25895_OK 成功；其他值失败
 */
int32_t bq25895_enter_ship_mode(void)
{
    int32_t ret;

    /* 禁用充电和 OTG */
    ret = bq25895_set_charge_enable(false);
    if (ret != BQ25895_OK) return ret;

    ret = bq25895_set_otg_config(false);
    if (ret != BQ25895_OK) return ret;

    /* 设置 10s 延时关断 BATFET */
    ret = bq25895_set_batfet_delay(BQ25895_BATFET_DLY_10S);
    if (ret != BQ25895_OK) return ret;

    /* 关断 BATFET */
    ret = bq25895_set_batfet_disable(true);
    if (ret != BQ25895_OK) return ret;

    return BQ25895_OK;
}

/**
 * @brief   退出运输模式 (重新使能 BATFET)
 * @return  BQ25895_OK 成功；其他值失败
 */
int32_t bq25895_exit_ship_mode(void)
{
    return bq25895_set_batfet_disable(false);
}

/**
 * @brief   使能 OTG 升压模式
 * @param   voltage_mv  升压输出电压 (mV). 范围: 4550 ~ 5510
 * @param   freq        升压频率
 * @return  BQ25895_OK 成功；其他值失败
 */
int32_t bq25895_enable_otg(uint16_t voltage_mv, bq25895_boost_freq_t freq)
{
    int32_t ret;

    ret = bq25895_set_boost_voltage(voltage_mv);
    if (ret != BQ25895_OK) return ret;

    ret = bq25895_set_boost_frequency(freq);
    if (ret != BQ25895_OK) return ret;

    /* 禁能充电 */
    ret = bq25895_set_charge_enable(false);
    if (ret != BQ25895_OK) return ret;

    /* 使能 OTG */
    ret = bq25895_set_otg_config(true);
    if (ret != BQ25895_OK) return ret;

    return BQ25895_OK;
}

/**
 * @brief   禁能 OTG 模式，恢复充电模式
 * @return  BQ25895_OK 成功；其他值失败
 */
int32_t bq25895_disable_otg(void)
{
    return bq25895_set_otg_config(false);
}

/**
 * @brief   快速初始化 (设置常用充电参数并开始充电)
 * @details 配置输入限流、充电电压/电流、预充电/终止电流、看门狗等。
 * @param   input_current_ma    输入限流 (mA)
 * @param   charge_voltage_mv   充电电压 (mV)
 * @param   charge_current_ma   充电电流 (mA)
 * @return  BQ25895_OK 成功；其他值失败
 */
int32_t bq25895_quick_init(uint16_t input_current_ma,
                            uint16_t charge_voltage_mv,
                            uint16_t charge_current_ma)
{
    int32_t ret;

    /* 禁用充电以安全配置 */
    ret = bq25895_set_charge_enable(false);
    if (ret != BQ25895_OK) return ret;

    /* 配置输入限流 */
    ret = bq25895_set_input_current_limit(input_current_ma);
    if (ret != BQ25895_OK) return ret;

    /* 配置充电参数 */
    ret = bq25895_configure_charge_params(charge_voltage_mv, charge_current_ma,
                                           BQ25895_DEFAULT_PRECHARGE_CURRENT_MA,
                                           BQ25895_DEFAULT_TERM_CURRENT_MA);
    if (ret != BQ25895_OK) return ret;

    /* 配置看门狗 40s */
    ret = bq25895_set_watchdog(BQ25895_WDT_40S);
    if (ret != BQ25895_OK) return ret;

    /* 使能充电终止 */
    ret = bq25895_set_termination_enable(true);
    if (ret != BQ25895_OK) return ret;

    /* 使能安全定时器 */
    ret = bq25895_set_safety_timer_enable(true);
    if (ret != BQ25895_OK) return ret;

    /* 使能充电 */
    ret = bq25895_set_charge_enable(true);
    if (ret != BQ25895_OK) return ret;

    return BQ25895_OK;
}
