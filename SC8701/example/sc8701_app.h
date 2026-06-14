/**
 * @file sc8701_app.h
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief SC8701 应用测试流程 — 覆盖使能/调压/限流/PG/故障全部核心功能
 * @version 1.0
 * @date 2026-06-13
 *
 * @note 参考 bq25710_app.h 架构，适配 SC8701 纯模拟控制器的特点。
 *       测试流程通过 GPIO/PWM/ADC 外设逐项验证芯片功能。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef SC8701_APP_H
#define SC8701_APP_H

#include "sc8701.h"

/* ==================== 应用配置常量 ==================== */

#define SC8701_APP_PWM_VOUT_FREQ_HZ   50000U   /**< VOUT 调压 PWM 频率 */
#define SC8701_APP_PWM_ILIM_FREQ_HZ   50000U   /**< IPWM 限流 PWM 频率 */
#define SC8701_APP_ENABLE_TIMEOUT_MS  500U     /**< 使能等待 PG 超时 */
#define SC8701_APP_STAGE_TIMEOUT_MS   5000U    /**< 单个阶段超时 */
#define SC8701_APP_MONITOR_INTERVAL_MS 5000U   /**< 监测打印间隔 */

/* ==================== 测试状态机枚举 ==================== */

/**
 * @brief SC8701 应用测试状态机
 *
 * 测试流程：
 *   INIT → CONFIGURE → ENABLE_TEST → VOUT_SWEEP_TEST →
 *   ILIM_TEST → DYNAMIC_TEST → MONITOR → IDLE
 */
enum sc8701_app_states_e {

    /* --- 初始化阶段 --- */
    SC_APP_STATE_INIT,              /**< GPIO/PWM 外设初始化，驱动初始化 */

    /* --- 配置阶段 --- */
    SC_APP_STATE_CONFIGURE,         /**< 设置初始输出电压和限流值 */

    /* --- 使能测试阶段 --- */
    SC_APP_STATE_ENABLE_TEST,       /**< 使能芯片，等待 PG，验证启动 */

    /* --- 调压扫描测试阶段 --- */
    SC_APP_STATE_VOUT_SWEEP_TEST,   /**< 扫描输出电压范围，验证调压功能 */

    /* --- 限流测试阶段 --- */
    SC_APP_STATE_ILIM_TEST,         /**< 动态调节电流限制，验证限流功能 */

    /* --- 动态响应测试阶段 --- */
    SC_APP_STATE_DYNAMIC_TEST,      /**< 负载/输入突变模拟，验证动态响应 */

    /* --- 监测阶段 --- */
    SC_APP_STATE_MONITOR,           /**< 持续监测 VIN/VOUT/IOUT/PG 状态 */

    /* --- 终态 --- */
    SC_APP_STATE_IDLE,              /**< 测试完成 */
    SC_APP_STATE_TIMEOUT,           /**< 超时错误 */
    SC_APP_STATE_ERROR,             /**< 致命错误 */

    SC_APP_STATE_COUNT
};

/* ==================== 结构体定义 ==================== */

/**
 * @brief 测试阶段耗时记录
 */
typedef struct {
    uint32_t enter_ms;              /**< 进入该阶段的时间戳 (ms) */
    uint32_t elapsed_ms;            /**< 该阶段耗时 (ms) */
} sc8701_app_stage_time_t;

/**
 * @brief 测试结果汇总 (位域)
 */
typedef struct {
    uint8_t init_ok        : 1;    /**< 驱动初始化成功 */
    uint8_t config_ok      : 1;    /**< 参数配置成功 */
    uint8_t enable_ok      : 1;    /**< 芯片使能成功 (PG=1) */
    uint8_t vout_sweep_ok  : 1;    /**< 调压扫描完成 */
    uint8_t ilim_ok        : 1;    /**< 限流测试完成 */
    uint8_t dynamic_ok     : 1;    /**< 动态响应测试完成 */
    uint8_t monitor_ok     : 1;    /**< 监测正常 */
    uint8_t reserved       : 1;
} sc8701_app_test_result_t;

/**
 * @brief ADC 监测数据缓存
 */
typedef struct {
    uint16_t vin_mv;                /**< 输入电压 (mV) */
    uint16_t vout_mv;               /**< 输出电压 (mV) */
    uint16_t iin_ma;                /**< 输入电流 (mA) */
    uint16_t iout_ma;               /**< 输出电流 (mA) */
    uint8_t  pg;                    /**< Power Good */
    sc8701_mode_t mode;             /**< 工作模式 */
    uint32_t timestamp_ms;          /**< 采样时间戳 */
} sc8701_app_adc_snapshot_t;

/**
 * @brief VOUT 扫描测试配置
 */
typedef struct {
    uint32_t start_mv;              /**< 起始电压 (mV) */
    uint32_t end_mv;                /**< 终止电压 (mV) */
    uint32_t step_mv;               /**< 步进 (mV) */
    uint32_t dwell_ms;              /**< 每步停留时间 (ms) */
    uint32_t current_mv;            /**< 当前电压 (mV) */
    uint8_t  direction;             /**< 0=正向扫描, 1=反向扫描 */
} sc8701_app_vout_sweep_t;

/**
 * @brief 事件回调
 */
typedef void (*sc8701_app_notify_handler)(uint8_t event, void *arg);

/* ==================== 应用主结构体 ==================== */

/**
 * @brief SC8701 应用全局上下文
 */
typedef struct {
    /* --- 状态机 --- */
    uint8_t task_state;                     /**< 当前状态 */
    uint8_t last_state;                     /**< 上次状态 */
    uint8_t timeout_state;                  /**< 超时后跳转状态 */
    uint8_t sub_step;                       /**< 当前阶段的子步骤 */

    /* --- 回调 --- */
    sc8701_app_notify_handler notify_handler; /**< 事件回调 */

    /* --- 驱动上下文 --- */
    sc8701_ctx_t  drv_ctx;                  /**< SC8701 驱动上下文 */
    sc8701_hal_t  hal;                      /**< 硬件抽象层 (运行时绑定) */
    sc8701_hw_config_t hw_config;           /**< 硬件配置 */

    /* --- 测试结果 --- */
    sc8701_app_test_result_t result;        /**< 测试结果汇总 */
    sc8701_app_stage_time_t  stages[SC_APP_STATE_COUNT]; /**< 各阶段耗时 */

    /* --- ADC 数据 --- */
    sc8701_app_adc_snapshot_t adc_snap;     /**< 最新 ADC 采样 */

    /* --- 调压扫描 --- */
    sc8701_app_vout_sweep_t vout_sweep;     /**< VOUT 扫描上下文 */

    /* --- 超时 --- */
    uint64_t timeout;                       /**< 超时时刻 */
    uint32_t last_monitor_ms;               /**< 上次监测打印时刻 */

} sc8701_app_t;

/* ==================== 公开接口 ==================== */

/**
 * @brief 初始化 SC8701 应用
 *
 * 设置初始状态为 INIT，绑定硬件抽象层回调。
 * 需在主循环前调用一次。
 */
void sc8701_app_init (void);

/**
 * @brief SC8701 应用主处理函数 (需在主循环中周期性调用)
 *
 * 状态机驱动函数，按序完成各阶段测试。
 * 调用频率建议 ≥ 10Hz。
 */
void sc8701_app_process (void);

/**
 * @brief 获取应用全局实例指针
 */
sc8701_app_t *sc8701_app_get_instance (void);

/**
 * @brief 打印当前状态汇总
 */
void sc8701_app_print_status (void);

#endif /* SC8701_APP_H */
