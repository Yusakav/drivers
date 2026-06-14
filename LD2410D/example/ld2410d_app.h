/**
 * @file ld2410d_app.h
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief LD2410D 应用层 — 初始化、周期性读取目标状态、运动/静止目标信息
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 */

#ifndef LD2410D_APP_H
#define LD2410D_APP_H

#include "ld2410d_drv.h"

/**
 * @brief LD2410D 应用状态机
 */
enum ld2410d_app_states_e {

    /* --- 初始化阶段 --- */
    LD2410D_STATE_INIT,              /**< UART / GPIO 初始化, 等待驱动就绪 */

    /* --- 设备检测阶段 --- */
    LD2410D_STATE_CHECK,             /**< 使能配置, 读取固件版本 & 序列号 */

    /* --- 参数配置阶段 --- */
    LD2410D_STATE_CONFIGURE,         /**< 设置探测距离 / 门限 / 消失延迟 */

    /* --- 正常运行阶段 --- */
    LD2410D_STATE_OPERATION,         /**< 周期性读取工程模式数据, 上报目标状态 */

    /* --- 终态 --- */
    LD2410D_STATE_IDLE,              /**< 测试完成 */
    LD2410D_STATE_TIMEOUT,           /**< 超时错误 */
    LD2410D_STATE_ERROR,             /**< 致命错误 */

    LD2410D_STATE_COUNT
};

/**
 * @brief 应用阶段耗时记录
 */
typedef struct {
    uint32_t enter_ms;              /**< 进入该阶段的时间戳 */
    uint32_t elapsed_ms;            /**< 该阶段耗时 */
} ld2410d_stage_time_t;

/**
 * @brief 应用结果汇总
 */
typedef struct {
    uint8_t init_ok        : 1;    /**< 初始化成功 */
    uint8_t version_ok     : 1;    /**< 固件版本读取成功 */
    uint8_t sn_ok          : 1;    /**< 序列号读取成功 */
    uint8_t config_ok      : 1;    /**< 参数配置成功 */
    uint8_t output_ok      : 1;    /**< 输出模式切换成功 */
    uint8_t detected       : 1;    /**< 至少检测到一次目标 */
    uint16_t reserved      : 10;
} ld2410d_app_result_t;

/**
 * @brief 目标状态回调
 *
 * 当检测目标状态发生变化时回调
 *
 * @param status   当前检测状态
 * @param distance 目标距离 (cm), 无人时值为 0
 * @param arg      用户自定义参数
 */
typedef void (*ld2410d_target_callback) (enum ld2410d_detect_status_e status,
                                          uint16_t distance,
                                          void *arg);

/**
 * @brief 应用全局实例
 */
struct ld2410d_app_t {
    enum ld2410d_app_states_e  task_state;         /**< 当前状态 */
    enum ld2410d_app_states_e  last_state;         /**< 上次状态 */
    enum ld2410d_app_states_e  timeout_state;      /**< 超时后跳转状态 */

    ld2410d_target_callback    target_callback;    /**< 目标状态回调 */
    void                      *target_cb_arg;      /**< 回调用户参数 */

    ld2410d_app_result_t       result;             /**< 结果汇总 */
    ld2410d_stage_time_t       stages[LD2410D_STATE_COUNT]; /**< 各阶段耗时 */

    uint8_t                    sub_step;           /**< 当前阶段的子步骤 */

    uint64_t                   timeout;            /**< 超时时刻 (毫秒时间戳) */
    uint64_t                   last_report_ms;     /**< 上次上报时间 */

    /* 缓存 */
    char                       fw_version[16];     /**< 固件版本字符串 */
    char                       sn_str[16];         /**< 序列号字符串 */
    uint8_t                    sn_hex[4];          /**< 序列号 (十六进制) */

    enum ld2410d_detect_status_e last_status;      /**< 上次检测状态 */
    uint16_t                      last_distance;   /**< 上次目标距离 */

    struct ld2410d_dev_t       dev;                /**< 驱动设备句柄 */
};

extern struct ld2410d_app_t ld2410d;

/**
 * @brief 初始化 LD2410D 应用
 *
 * 设置初始状态, 注册 UART 操作集和回调。
 * 需在 ld2410d.uart 中填充 send / recv / flush 后再调用。
 */
void ld2410d_app_init (void);

/**
 * @brief 处理 LD2410D 应用 (需在主循环中周期性调用)
 *
 * 状态机驱动函数, 按序完成: 初始化 → 检测 → 配置 → 运行
 */
void ld2410d_app_process (void);

#endif /* LD2410D_APP_H */
