/**
 * @file bq25710_example_learn.h
 * @brief BQ25710 学习模式与电池容量估算 - 公共接口
 *
 * 学习模式允许电池在适配器存在的情况下放电,
 * 用于电量计校准和电池容量估算。
 *
 * 核心能力:
 *   - 进入/退出学习模式 (EN_LEARN)
 *   - IADPT 放大器增益配置 (20x/40x, 用于适配器电流监测)
 *   - 完整充放电循环管理 (CC充电→CV充电→放电→容量记录)
 *   - 库仑计数框架 (ADC 周期性采样积分)
 *   - 充电效率估算
 *
 * @note 基于已修复的 bq25710.h 驱动API
 */

#ifndef BQ25710_EXAMPLE_LEARN_H
#define BQ25710_EXAMPLE_LEARN_H

#include <stdint.h>
#include "bq25710.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * IADPT 增益配置 (ChargeOption0 bit[4])
 *
 * 影响 IADPT 引脚模拟输出电压:
 *   IADPT_GAIN=0 (默认): 20x, V_IADPT = 20 × I_RAC × R_AC
 *   IADPT_GAIN=1:        40x, V_IADPT = 40 × I_RAC × R_AC
 *
 * 10mΩ RAC 示例:
 *   20x: I_IN=3.25A → V_IADPT = 20 × 3.25A × 0.01Ω = 0.65V
 *   40x: I_IN=3.25A → V_IADPT = 40 × 3.25A × 0.01Ω = 1.30V
 * ======================================================================== */

#define LEARN_IADPT_GAIN_20X   0  /**< 20x 增益 (默认) */
#define LEARN_IADPT_GAIN_40X   1  /**< 40x 增益 */

/* ========================================================================
 * IBAT 增益配置 (ChargeOption0 bit[3])
 *
 * 影响 IBAT 引脚模拟输出电压:
 *   IBAT_GAIN=0: 16x (默认, 修复后)
 *   IBAT_GAIN=1: 8x
 *
 * 10mΩ RSR 示例:
 *   16x: I_CHG=3A → V_IBAT = 16 × 3A × 0.01Ω = 0.48V
 *   8x:  I_CHG=3A → V_IBAT =  8 × 3A × 0.01Ω = 0.24V
 * ======================================================================== */

#define LEARN_IBAT_GAIN_16X   0  /**< 16x 增益 (默认) */
#define LEARN_IBAT_GAIN_8X    1  /**< 8x 增益 */

/* ========================================================================
 * 学习模式阶段枚举
 * ======================================================================== */

typedef enum {
    LEARN_STAGE_IDLE,            /**< 空闲 (学习模式未启动) */
    LEARN_STAGE_CC_CHARGE,       /**< 恒流充电阶段 */
    LEARN_STAGE_CV_CHARGE,       /**< 恒压充电阶段 */
    LEARN_STAGE_CHARGE_DONE,     /**< 充电完成 (满电) */
    LEARN_STAGE_DISCHARGE,       /**< 放电阶段 (学习模式放电) */
    LEARN_STAGE_DISCHARGE_DONE,  /**< 放电完成 (截止电压) */
    LEARN_STAGE_COMPLETE,        /**< 学习完成 */
    LEARN_STAGE_ERROR            /**< 错误状态 */
} learn_stage_t;

/* ========================================================================
 * 电池容量学习数据结构体
 * ======================================================================== */

typedef struct {
    learn_stage_t stage;            /**< 当前阶段 */
    uint32_t      stage_timestamp;  /**< 阶段开始时间戳 (ms) */

    /* 充电阶段数据 */
    float    charge_energy_mwh;     /**< 充电输入能量 (mWh) */
    float    charge_capacity_mah;   /**< 充电容量 (库仑计数, mAh) */
    uint32_t cc_duration_ms;        /**< CC 阶段持续时间 (ms) */
    uint32_t cv_duration_ms;        /**< CV 阶段持续时间 (ms) */

    /* 放电阶段数据 */
    float    discharge_energy_mwh;  /**< 放电输出能量 (mWh) */
    float    discharge_capacity_mah; /**< 放电容量 (库仑计数, mAh) */
    uint32_t discharge_duration_ms; /**< 放电持续时间 (ms) */

    /* 效率估算 */
    float    charging_efficiency;   /**< 充电效率 = E_out / E_in */

    /* 学习参数 */
    uint16_t charge_voltage_mv;     /**< 充电终止电压 (mV) */
    uint16_t charge_current_ma;     /**< 充电电流 (mA) */
    uint16_t termination_current_ma; /**< 充电终止电流 (mA) */
    uint16_t discharge_cutoff_mv;   /**< 放电截止电压 (mV) */
} learn_session_t;

/* ========================================================================
 * 公共 API 声明
 * ======================================================================== */

/**
 * @brief 进入学习模式
 *
 * 配置 ChargeOption0 bit[5] EN_LEARN=1,
 * 允许电池在适配器存在时放电。
 *
 * 同时配置 IADPT 增益以便精确监测适配器电流。
 *
 * @param iadpt_gain IADPT 增益: LEARN_IADPT_GAIN_20X 或 LEARN_IADPT_GAIN_40X
 * @return int8_t BQ25710_OK 或错误码
 */
int8_t learn_enter(uint8_t iadpt_gain);

/**
 * @brief 退出学习模式并恢复充电
 *
 * EN_LEARN=0, 恢复 ChargeOption0 默认配置。
 *
 * @return int8_t BQ25710_OK 或错误码
 */
int8_t learn_exit(void);

/**
 * @brief 开始完整充放电学习循环
 *
 * 流程:
 *   1. CC 充电至终止电压 (I_CHG 恒定)
 *   2. CV 充电至终止电流 (C/10)
 *   3. 进入学习模式放电
 *   4. 放电至截止电压
 *   5. 记录累积容量和能量
 *
 * @param session        学习会话结构体指针
 * @param charge_voltage_mv  充电终止电压 (mV)
 * @param charge_current_ma  充电电流 (mA)
 * @param discharge_cutoff_mv 放电截止电压 (mV)
 * @return int8_t
 */
int8_t learn_start_cycle(learn_session_t *session,
                         uint16_t charge_voltage_mv,
                         uint16_t charge_current_ma,
                         uint16_t discharge_cutoff_mv);

/**
 * @brief 库仑计数积分 - 周期性调用 (如每100ms)
 *
 * 通过 ADC 采样 IBAT (充电电流) 和 IADPT (适配器电流),
 * 对电流进行时间积分, 累加容量 (mAh)。
 *
 * 库仑计数公式:
 *   ΔQ = I × Δt
 *   充电时: Q += I_CHG × Δt
 *   放电时: Q -= I_DCHG × Δt
 *
 * 累加能量:
 *   ΔE_charge = I_CHG × V_BAT × Δt
 *   ΔE_discharge = I_DCHG × V_BAT × Δt
 *
 * @param session  学习会话结构体指针
 * @param dt_ms    本次积分时间间隔 (ms)
 * @return int8_t
 */
int8_t learn_coulomb_count(learn_session_t *session, uint32_t dt_ms);

/**
 * @brief 充电效率估算
 *
 * η = E_discharge / E_charge × 100%
 *
 * 同时计算功率级效率:
 * η_power = (I_BAT × V_BAT) / (I_IN × V_IN) × 100%
 *
 * @param session 学习会话结构体指针
 * @return int8_t
 */
int8_t learn_calc_efficiency(learn_session_t *session);

/**
 * @brief 保存学习结果 (电池参数)
 *
 * 将估算的电池容量、效率、充放电参数写入 NVM/EEPROM。
 *
 * @param session 学习会话结构体指针
 * @return int8_t
 */
int8_t learn_save_params(const learn_session_t *session);

/**
 * @brief 完整学习模式演示
 *
 * 演示:
 *   1. 配置充电参数
 *   2. CC 充电 → CV 充电 → 满电
 *   3. 进入学习模式放电
 *   4. 库仑计数积分模拟
 *   5. 效率计算
 *   6. 退出学习模式
 */
void learn_demo(void);

#ifdef __cplusplus
}
#endif

#endif /* BQ25710_EXAMPLE_LEARN_H */
