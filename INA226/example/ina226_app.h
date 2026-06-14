/**
 * @file ina226_app.h
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief INA226 应用层 — 状态机驱动电源监控示例
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 *
 * @details 模拟典型电池管理系统 (BMS) 或电源监控场景:
 *          - 周期性采集总线电压/电流/功率
 *          - 过压/欠压/过流报警与保护逻辑
 *          - GPIO 绑定 (SCL/SDA/ALERT)
 *          - 状态机驱动架构，每个状态内部使用 sub_step 推进
 */

#ifndef INA226_APP_H
#define INA226_APP_H

#include <stdint.h>
#include "ina226.h"

/* ==================== GPIO 引脚定义 ==================== */

#define INA226_PIN_SCL_PORT     GPIOB
#define INA226_PIN_SCL_PIN      10
#define INA226_PIN_SDA_PORT     GPIOB
#define INA226_PIN_SDA_PIN      11
#define INA226_PIN_ALERT_PORT   GPIOB
#define INA226_PIN_ALERT_PIN    12


/**
 * @brief GPIO 原子操作宏 (假设 GPIO 库提供)
 */

#define INA226_SCL_H()          GPIO_SetBit(INA226_PIN_SCL_PORT, INA226_PIN_SCL_PIN)
#define INA226_SCL_L()          GPIO_ResetBit(INA226_PIN_SCL_PORT, INA226_PIN_SCL_PIN)
#define INA226_SDA_H()          GPIO_SetBit(INA226_PIN_SDA_PORT, INA226_PIN_SDA_PIN)
#define INA226_SDA_L()          GPIO_ResetBit(INA226_PIN_SDA_PORT, INA226_PIN_SDA_PIN)
#define INA226_SCL_READ()       GPIO_ReadInputPin(INA226_PIN_SCL_PORT, INA226_PIN_SCL_PIN)
#define INA226_SDA_READ()       GPIO_ReadInputPin(INA226_PIN_SDA_PORT, INA226_PIN_SDA_PIN)
#define INA226_ALERT_READ()     GPIO_ReadInputPin(INA226_PIN_ALERT_PORT, INA226_PIN_ALERT_PIN)

/* ==================== 采样电阻与系统参数 ==================== */

/**
 * @brief 采样电阻值 (Ω)
 *
 * 典型 BMS / 电源监控场景使用 0.01Ω (10mΩ) 或 0.1Ω。
 * 0.01Ω = 10mΩ → 最大可测电流 ≈ 8.192A (81.92mV ÷ 10mΩ)
 */
#define INA226_APP_RSHUNT_OHM           0.01f

/**
 * @brief 最大期望电流 (A)
 *
 * 用于校准计算 Current_LSB = MaxExpectedCurrent / 2^15
 */
#define INA226_APP_MAX_CURRENT_A        8.192f

/* ==================== 保护阈值 ==================== */

/** @brief 总线电压过压阈值 (V)，超过则触发保护 */
#define INA226_APP_VBUS_OV_THRESHOLD_V  14.0f

/** @brief 总线电压欠压阈值 (V)，低于则触发保护 */
#define INA226_APP_VBUS_UV_THRESHOLD_V  9.0f

/** @brief 电流过流阈值 (A)，超过则触发保护 */
#define INA226_APP_CURRENT_OC_THRESHOLD_A 5.0f

/** @brief 功率超限阈值 (W) */
#define INA226_APP_POWER_OVER_THRESHOLD_W 60.0f

/** @brief 报警去抖动次数 (连续 N 次超限才确认) */
#define INA226_APP_ALERT_DEBOUNCE_CNT   3

/* ==================== 状态机枚举 ==================== */

typedef enum {
    INA226_STATE_INIT = 0,          /**< 初始化: GPIO / I2C / 驱动  */
    INA226_STATE_CHECK_ID,          /**< 器件 ID 校验               */
    INA226_STATE_CONFIGURE,         /**< 配置采样参数与校准          */
    INA226_STATE_MONITOR,           /**< 持续电源监控                */
    INA226_STATE_CHECK_ALERT,       /**< 检查报警状态                */
    INA226_STATE_DUMP,              /**< 转储寄存器 (调试)           */
    INA226_STATE_IDLE,              /**< 空闲                        */
    INA226_STATE_TIMEOUT,           /**< 超时跳转                    */
    INA226_STATE_ERROR,             /**< 错误状态                    */

    INA226_STATE_COUNT
} ina226_app_state_t;

/* ==================== 数据结构 ==================== */

/**
 * @brief 阶段耗时结构体
 */
typedef struct {
    uint32_t start_ms;               /**< 阶段起始时间戳 (ms)        */
    uint32_t elapsed_ms;             /**< 阶段已耗时 (ms)            */
    uint32_t timeout_ms;             /**< 阶段超时阈值 (ms)          */
} ina226_stage_time_t;

/**
 * @brief 监控结果汇总
 */
typedef struct {
    float    vbus_max_V;            /**< 总线电压最大值 (V)          */
    float    vbus_min_V;            /**< 总线电压最小值 (V)          */
    float    vbus_avg_V;            /**< 总线电压平均值 (V)          */
    float    current_max_A;         /**< 电流最大值 (A)              */
    float    current_min_A;         /**< 电流最小值 (A)              */
    float    current_avg_A;         /**< 电流平均值 (A)              */
    float    power_max_W;           /**< 功率最大值 (W)              */
    float    power_avg_W;           /**< 功率平均值 (W)              */
    uint32_t sample_count;          /**< 累计采样次数                */
    uint32_t alert_count;           /**< 累计报警次数                */
    uint32_t overflow_count;        /**< 累计溢出次数                */
    uint8_t  vbus_ov_triggered : 1; /**< 过压保护触发标志            */
    uint8_t  vbus_uv_triggered : 1; /**< 欠压保护触发标志            */
    uint8_t  current_oc_triggered:1;/**< 过流保护触发标志            */
} ina226_monitor_result_t;

/**
 * @brief 事件通知回调类型
 *
 * @param state    当前状态
 * @param sub_step 当前子步骤
 * @param arg      用户自定义参数
 * @param message  事件消息字符串
 */
typedef void (*ina226_notify_handler)(ina226_app_state_t state,
                                       uint8_t sub_step,
                                       void *arg,
                                       const char *message);

/**
 * @brief INA226 应用实例结构体
 */
typedef struct {
    ina226_app_state_t   task_state;       /**< 当前主状态                   */
    ina226_app_state_t   last_state;       /**< 上一个状态                   */
    ina226_app_state_t   timeout_state;    /**< 超时后跳转的目标状态          */

    ina226_notify_handler notify_handler;   /**< 事件通知回调                */
    void                *notify_arg;       /**< 回调用户参数                 */

    ina226_monitor_result_t result;        /**< 监控结果汇总                 */
    ina226_stage_time_t stages[INA226_STATE_COUNT];

    uint8_t  sub_step;                     /**< 当前状态的子步骤计数         */
    uint32_t last_now_ms;                  /**< 上一轮 now_ms 快照           */

    uint32_t timeout_ms;                   /**< 当前超时时长                 */
    uint32_t timeout_start_ms;             /**< 超时计时起点                 */
} ina226_app_t;

/* ==================== LOG 宏定义 ==================== */

#ifndef LOG_TAG
    #define LOG_TAG_SAVED   ""
#else
    #define LOG_TAG_SAVED   LOG_TAG
#endif
#undef LOG_TAG
#define LOG_TAG     "INA App"

#define LOG_I(fmt, ...) printf("[I][%s] " fmt "\n", LOG_TAG, ##__VA_ARGS__)
#define LOG_E(fmt, ...) printf("[E][%s] " fmt "\n", LOG_TAG, ##__VA_ARGS__)

#undef LOG_TAG
#define LOG_TAG     LOG_TAG_SAVED

/* ==================== API 函数声明 ==================== */

/**
 * @brief 初始化 INA226 应用实例
 *
 * 设置默认状态、超时阈值、重置 sub_step。
 *
 * @param app 应用实例指针
 */
void ina226_app_init(ina226_app_t *app);

/**
 * @brief INA226 应用状态机主处理函数
 *
 * 每轮循环调用一次，需传入当前系统毫秒时间戳。
 *
 * @param app    应用实例指针
 * @param now_ms 系统毫秒时间戳
 */
void ina226_app_process(ina226_app_t *app, uint32_t now_ms);

#endif /* INA226_APP_H */
