/**
 * @file    bq25895_app.h
 * @brief   BQ25895 充电管理应用层示例 - 头文件
 * @details 定义充电管理上下文、电池参数配置、充电阶段状态机及 API 声明。
 * @version 1.0.0
 * @date    2026-06-13
 */

#ifndef __BQ25895_APP_H__
#define __BQ25895_APP_H__

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * INCLUDES
 *===========================================================================*/
#include "bq25895.h"
#include <stdint.h>
#include <stdbool.h>

/*===========================================================================
 * BATTERY CHEMISTRY / CELL COUNT DEFAULTS
 *===========================================================================*/

/** @brief 单节锂离子/锂聚合物电池典型参数 */
#define BQ25895_APP_BAT_VOLTAGE_NORMAL_MV      4208U   /**< 标准充电电压: 4.208V */
#define BQ25895_APP_BAT_VOLTAGE_HIGH_MV        4352U   /**< 高压电池充电电压: 4.352V */
#define BQ25895_APP_BAT_VOLTAGE_LOW_MV         3840U   /**< 最低可设充电电压: 3.840V */

/** @brief 充电电流预设 */
#define BQ25895_APP_CHG_CURRENT_500MA          500U    /**< USB 标准充电 */
#define BQ25895_APP_CHG_CURRENT_1000MA         1000U   /**< 1A 充电 */
#define BQ25895_APP_CHG_CURRENT_1500MA         1500U   /**< 1.5A 充电 */
#define BQ25895_APP_CHG_CURRENT_2000MA         2000U   /**< 2A 充电 (默认) */
#define BQ25895_APP_CHG_CURRENT_3000MA         3000U   /**< 3A 快速充电 */
#define BQ25895_APP_CHG_CURRENT_5000MA         5000U   /**< 5A 最大充电 */

/** @brief 输入限流预设 */
#define BQ25895_APP_INPUT_CUR_500MA            500U
#define BQ25895_APP_INPUT_CUR_1000MA           1000U
#define BQ25895_APP_INPUT_CUR_1500MA           1500U
#define BQ25895_APP_INPUT_CUR_2000MA           2000U
#define BQ25895_APP_INPUT_CUR_3000MA           3000U

/** @brief 预充电电流 */
#define BQ25895_APP_PRECHARGE_CUR_128MA        128U
#define BQ25895_APP_PRECHARGE_CUR_256MA        256U

/** @brief 终止充电电流 */
#define BQ25895_APP_TERM_CUR_128MA             128U
#define BQ25895_APP_TERM_CUR_256MA             256U

/** @brief OTG 输出电压预设 */
#define BQ25895_APP_OTG_VOLTAGE_5V0            5006U   /**< 5.006V (8 = 4550+8*64=5062, 取约5V) */
#define BQ25895_APP_OTG_VOLTAGE_5V1            5126U   /**< 5.126V (9 = 默认) */
#define BQ25895_APP_OTG_VOLTAGE_5V5            5510U   /**< 5.51V (最大) */

/** @brief 电池温度阈值 TS 百分比 */
#define BQ25895_APP_TS_COLD_PCT                7275U   /**< ~72.75% (0°C w/ 103AT + RT1/RT2) */
#define BQ25895_APP_TS_COOL_PCT                7000U   /**< ~70.00% (10°C) */
#define BQ25895_APP_TS_HOT_PCT                 3400U   /**< ~34.00% (45°C w/ 103AT) */
#define BQ25895_APP_TS_EXTREME_HOT_PCT         3000U   /**< ~30.00% (55°C) */

/*===========================================================================
 * ENUMERATIONS
 *===========================================================================*/

/** @brief 充电管理阶段 */
typedef enum {
    BQ25895_CHARGE_PHASE_IDLE        = 0,  /**< 空闲: 等待适配器插入 */
    BQ25895_CHARGE_PHASE_DETECT      = 1,  /**< 检测: 确认适配器类型和电池状态 */
    BQ25895_CHARGE_PHASE_PRECHARGE   = 2,  /**< 预充电: 电池电压过低，小电流预充 */
    BQ25895_CHARGE_PHASE_CC          = 3,  /**< 恒流充电: 快速充电 (CC) */
    BQ25895_CHARGE_PHASE_CV          = 4,  /**< 恒压充电: 电压平台 (CV) */
    BQ25895_CHARGE_PHASE_DONE        = 5,  /**< 充电完成: 达到终止条件 */
    BQ25895_CHARGE_PHASE_FAULT       = 6,  /**< 故障处理: 待清理故障 */
    BQ25895_CHARGE_PHASE_OTG         = 7,  /**< OTG 升压模式 */
    BQ25895_CHARGE_PHASE_SHIP        = 8,  /**< 运输模式 (低功耗) */
} bq25895_charge_phase_t;

/** @brief OTG 输出电压档位 */
typedef enum {
    BQ25895_OTG_VOLTAGE_5V           = 0,
    BQ25895_OTG_VOLTAGE_5V1          = 1,
    BQ25895_OTG_VOLTAGE_5V5          = 2,
} bq25895_otg_voltage_t;

/*===========================================================================
 * DATA STRUCTURES
 *===========================================================================*/

/** @brief 电池参数配置 */
typedef struct {
    uint16_t  charge_voltage_mv;       /**< 目标充电电压 (mV) */
    uint16_t  charge_current_ma;       /**< 快充充电电流 (mA) */
    uint16_t  precharge_current_ma;    /**< 预充电电流 (mA) */
    uint16_t  termination_current_ma;  /**< 终止充电电流 (mA) */
    uint16_t  input_current_ma;        /**< 输入限流 (mA) */
    uint16_t  sys_min_mv;              /**< 系统最低电压 (mV) */
    bool      enable_term;             /**< 使能充电终止 */
    bool      enable_safety_timer;     /**< 使能安全定时器 */
    bq25895_chg_timer_t  chg_timer;    /**< 充电安全定时器时长 */
    bq25895_watchdog_t   watchdog;     /**< 看门狗配置 */
} bq25895_battery_config_t;

/** @brief 充电管理上下文 */
typedef struct {
    bq25895_charge_phase_t  phase;             /**< 当前充电阶段 */
    bq25895_charge_phase_t  prev_phase;        /**< 上一充电阶段 */
    bq25895_battery_config_t bat_cfg;          /**< 电池参数配置 */
    bq25895_status_t        status;            /**< 当前状态 */
    bq25895_fault_t         fault;             /**< 当前故障 */
    bq25895_adc_result_t    adc;               /**< ADC 读数 */
    uint32_t                phase_enter_ms;    /**< 进入当前阶段的时间戳 (ms) */
    uint32_t                last_wd_feed_ms;   /**< 上次喂狗时间戳 (ms) */
    uint32_t                sample_interval_ms;/**< 状态采样间隔 (ms) */
    uint8_t                 fault_retry_cnt;   /**< 故障重试计数 */
    bool                    adapter_present;   /**< 适配器插入标志 */
    bool                    charge_complete;   /**< 充电完成标志 */
    bool                    otg_requested;     /**< OTG 请求标志 */
    bq25895_otg_voltage_t   otg_voltage;       /**< OTG 输出电压档位 */
} bq25895_charge_ctx_t;

/*===========================================================================
 * GLOBAL INSTANCE (extern)
 *===========================================================================*/
extern bq25895_charge_ctx_t g_bq25895_ctx;

/*===========================================================================
 * PUBLIC API
 *===========================================================================*/

/**
 * @brief   初始化充电管理上下文
 * @param   ctx    充电管理上下文指针
 * @param   bat_cfg 电池参数配置指针
 * @return  BQ25895_OK 成功；其他值失败
 */
int32_t bq25895_app_init(bq25895_charge_ctx_t *ctx,
                          const bq25895_battery_config_t *bat_cfg);

/**
 * @brief   充电状态机主循环处理函数
 * @details 每次调用推进一个子步骤，按阶段分别调用 process_phase_xxx()。
 * @param   ctx              充电管理上下文指针
 * @param   current_time_ms  当前系统时间 (ms)
 * @return  BQ25895_OK 成功；其他值失败
 */
int32_t bq25895_app_process(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms);

/**
 * @brief   请求进入 OTG 模式
 * @param   ctx      充电管理上下文指针
 * @param   voltage  OTG 输出电压档位
 * @return  BQ25895_OK 成功；其他值失败
 */
int32_t bq25895_app_request_otg(bq25895_charge_ctx_t *ctx,
                                 bq25895_otg_voltage_t voltage);

/**
 * @brief   请求退出 OTG 模式回到充电管理
 * @param   ctx  充电管理上下文指针
 * @return  BQ25895_OK 成功；其他值失败
 */
int32_t bq25895_app_exit_otg(bq25895_charge_ctx_t *ctx);

/**
 * @brief   请求进入运输模式
 * @param   ctx  充电管理上下文指针
 * @return  BQ25895_OK 成功；其他值失败
 */
int32_t bq25895_app_request_ship_mode(bq25895_charge_ctx_t *ctx);

/**
 * @brief   获取充电阶段名称字符串
 * @param   phase  充电阶段
 * @return  阶段名称字符串
 */
const char* bq25895_app_phase_name(bq25895_charge_phase_t phase);

/**
 * @brief   获取 VBUS 源类型名称字符串
 * @param   vbus_stat  VBUS 状态
 * @return  名称字符串
 */
const char* bq25895_app_vbus_name(bq25895_vbus_stat_t vbus_stat);

#ifdef __cplusplus
}
#endif

#endif /* __BQ25895_APP_H__ */
