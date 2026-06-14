/**
 * @file pd_app.h
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief USB PD 应用层驱动 — Type-C 连接管理、PD 协商调度与 VBUS 监控
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef PD_APP_H
#define PD_APP_H

#include "usb_pd_protocol.h"

/**
 * @brief PD 应用层状态机枚举（应用层调度，区别于底层 PD 协议状态机）
 */
enum pd_app_states_e {
    PD_APP_STATE_INIT,           /**< 初始化 */
    PD_APP_STATE_DISCONNECTED,   /**< 等待 Type-C 连接 */
    PD_APP_STATE_DEBOUNCE,       /**< CC 去抖 */
    PD_APP_STATE_ATTACHED,       /**< 已附着，等待 PD 协商 */
    PD_APP_STATE_NEGOTIATING,    /**< PD 协商中 */
    PD_APP_STATE_READY,          /**< 供电就绪 */
    PD_APP_STATE_IDLE,           /**< 空闲 */
    PD_APP_STATE_TIMEOUT,        /**< 超时错误 */
    PD_APP_STATE_ERROR,          /**< 致命错误 */

    PD_APP_STATE_COUNT
};

/**
 * @brief 阶段耗时记录
 */
typedef struct {
    uint32_t enter_ms;          /**< 进入该阶段的时间戳 (ms) */
    uint32_t elapsed_ms;        /**< 该阶段耗时 (ms) */
} pd_app_stage_time_t;

/**
 * @brief PD 协商结果
 */
typedef struct {
    uint8_t  negotiated    : 1; /**< 是否协商成功 */
    uint32_t voltage_mv;        /**< 协商电压 (mV) */
    uint32_t current_ma;        /**< 协商电流 (mA) */
    uint32_t power_mw;          /**< 协商功率 (mW) */
    uint8_t  pdo_index;         /**< 选中的 PDO 索引 */
} pd_app_negotiation_t;

/**
 * @brief 事件回调类型
 */
typedef void (*pd_app_notify_handler)(uint8_t event, void *arg);

/**
 * @brief PD 应用全局实例
 */
struct pd_app_t {
    enum pd_app_states_e task_state;               /**< 当前状态 */
    enum pd_app_states_e last_state;               /**< 上次状态 */
    enum pd_app_states_e timeout_state;            /**< 超时后跳转状态 */

    pd_app_notify_handler notify_handler;          /**< 事件回调 */
    pd_app_stage_time_t   stages[PD_APP_STATE_COUNT]; /**< 各阶段耗时 */
    pd_app_negotiation_t  negotiation;             /**< 协商结果 */

    uint8_t  sub_step;                             /**< 当前阶段的子步骤 */
    int      init_ret;                             /**< 初始化返回值 (跨 sub_step 保持) */
    uint64_t timeout;                              /**< 超时时刻 (ms) */
    uint32_t vbus_mv;                              /**< VBUS 电压 (mV) */
    uint8_t  cc1_state;                            /**< CC1 电压状态 */
    uint8_t  cc2_state;                            /**< CC2 电压状态 */
    uint8_t  cc_polarity;                          /**< 当前有效的 CC 索引 (1=CC1, 2=CC2, 与底层 usb_pd_phy_set_sel 一致) */
    uint8_t  port;                                 /**< PD 端口号 */
};

extern struct pd_app_t pd_app;

void pd_app_init(int port);
void pd_app_process(void);

#endif /* PD_APP_H */
