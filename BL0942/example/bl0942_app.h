/**
 * @file bl0942_app.h
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief BL0942 应用层 — 基于状态机的电参数采集与监控
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 *
 * @note 参考 bq25710_app.c/h 的状态机架构设计。
 *       覆盖: 初始化 → 器件检测 → 配置 → 循环采集 → 故障处理 → 空闲。
 *
 * @note 硬件引脚定义 (示例, 按实际接线修改):
 *       BL0942_SEL         PB8      (0=UART, 1=SPI, 内部下拉)
 *       BL0942_TX_SDO      PB9      (UART TX / SPI DO)
 *       BL0942_RX_SDI      PB10     (UART RX / SPI DI)
 *       BL0942_SCLK_BPS    PB11     (SPI 时钟 / UART 波特率选择)
 *       BL0942_CF1         PA0      (电能脉冲 / 过流报警 / 过零 可选)
 */

#ifndef BL0942_APP_H
#define BL0942_APP_H

#include "bl0942.h"
#include <stdint.h>

/* ==================== 应用状态机 ==================== */

/** @brief BL0942 应用状态机枚举 */
typedef enum {
    BL0942_APP_STATE_INIT,          /**< 初始化: GPIO + 总线配置 + 驱动 init */
    BL0942_APP_STATE_CHECK,         /**< 器件检测: ping 通信测试 */
    BL0942_APP_STATE_CONFIGURE,     /**< 参数配置: 增益/阈值/MODE/输出功能 */
    BL0942_APP_STATE_MEASURE,       /**< 循环测量: 周期性读取电参数 */
    BL0942_APP_STATE_OVERCURRENT,   /**< 过流报警处理 */
    BL0942_APP_STATE_DUMP,          /**< 寄存器转储 */
    BL0942_APP_STATE_IDLE,          /**< 空闲 */
    BL0942_APP_STATE_ERROR,         /**< 错误状态 */

    BL0942_APP_STATE_COUNT
} bl0942_app_state_t;

/* ==================== 应用配置 ==================== */

/**
 * @brief BL0942 应用参数配置
 */
typedef struct {
    uint8_t  current_gain;          /**< 电流增益: BL0942_GAIN_1X/4X/16X/24X */
    uint16_t overcurrent_threshold; /**< 过流阈值 (与 I_FAST_RMS[23:8] 比较) */
    uint8_t  fast_rms_cycle;       /**< 快速有效值刷新周期: 0~4 */
    uint8_t  freq_cycle;           /**< 频率刷新周期: 0~3 */
    uint16_t mode;                 /**< MODE 寄存器值 (10bit) */
    uint8_t  cf1_func;             /**< CF1 输出功能 */
    uint8_t  cf2_func;             /**< CF2 输出功能 */
    uint8_t  zx_func;              /**< ZX 输出功能 */
    uint8_t  ac_freq_hz;           /**< 交流频率: 50 或 60 Hz */
    uint8_t  rms_update_400ms;     /**< 有效值刷新: 1=400ms, 0=800ms */
} bl0942_app_config_t;

/* ==================== 测试结果汇总 ==================== */

typedef struct {
    uint8_t init_ok       : 1;     /**< 驱动初始化成功 */
    uint8_t ping_ok       : 1;     /**< 通信检测通过 */
    uint8_t config_ok     : 1;     /**< 参数配置完成 */
    uint8_t measure_ok    : 1;     /**< 测量功能正常 */
    uint8_t overcurrent   : 1;     /**< 发生过流 */
    uint8_t creep_active  : 1;     /**< 防潜动激活 */
    uint8_t reverse_power : 1;     /**< 反接/负功率 */
    uint16_t reserved     : 9;
} bl0942_app_result_t;

/** @brief 事件回调 */
typedef void (*bl0942_app_notify_t)(uint8_t event, void *arg);

/* ==================== 阶段耗时记录 ==================== */

typedef struct {
    uint32_t enter_ms;
    uint32_t elapsed_ms;
} bl0942_stage_time_t;

/* ==================== 应用上下文 ==================== */

struct bl0942_app_t {
    bl0942_app_state_t  task_state;              /**< 当前状态 */
    bl0942_app_state_t  last_state;              /**< 上次状态 */
    bl0942_app_state_t  timeout_state;           /**< 超时后跳转状态 */

    struct bl0942_bus_t bus;                     /**< 通信总线 */
    bl0942_measurement_t meas;                   /**< 最新测量数据 */
    bl0942_app_config_t  config;                 /**< 应用配置 */
    bl0942_app_result_t  result;                 /**< 测试结果 */
    bl0942_stage_time_t  stages[BL0942_APP_STATE_COUNT]; /**< 各阶段耗时 */

    bl0942_app_notify_t notify_handler;          /**< 事件回调 */

    uint8_t  sub_step;                           /**< 当前阶段子步骤 */
    uint64_t timeout;                            /**< 超时时刻 */

    uint32_t last_measure_ms;                    /**< 上次测量时间 */
    uint32_t last_print_ms;                      /**< 上次打印时间 */
    uint32_t measure_interval_ms;                /**< 测量间隔 (默认 1000ms) */
    uint32_t print_interval_ms;                  /**< 打印间隔 (默认 5000ms) */

    uint8_t  overcurrent_cnt;                    /**< 连续过流计数 */
    uint8_t  overcurrent_threshold_cnt;          /**< 过流确认阈值 (连续 N 次) */
};

/* ==================== 公开接口 ==================== */

extern struct bl0942_app_t bl0942_app;

/**
 * @brief 初始化 BL0942 应用
 */
void bl0942_app_init(void);

/**
 * @brief BL0942 应用主循环 (需在主循环中周期性调用)
 */
void bl0942_app_process(void);

/**
 * @brief 获取最新测量数据的只读指针
 */
const bl0942_measurement_t *bl0942_app_get_measurement(void);

/**
 * @brief 设置测量间隔
 * @param measure_ms 测量间隔 (ms), 默认 1000
 * @param print_ms   打印间隔 (ms), 默认 5000
 */
void bl0942_app_set_interval(uint32_t measure_ms, uint32_t print_ms);

/**
 * @brief 设置通知回调
 * @param handler 回调函数 (NULL 取消)
 */
void bl0942_app_set_notify(bl0942_app_notify_t handler);

#endif /* BL0942_APP_H */
