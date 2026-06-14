/**
 * @file    zw101_fp_app.h
 * @brief   ZW101 指纹模块应用层 — 状态机驱动的演示程序
 * @note    参考 bq25710_app.h 的架构风格: 分层状态机、子步骤、超时监控
 * @version 1.0
 * @date    2026-06-13
 *
 * ## 状态机设计
 * ```
 * INIT → SELF_TEST → DEMO_SELECT → ENROLL_DEMO → VERIFY_DEMO → DELETE_DEMO
 *                                                      ↓
 *                                                   IDLE (等待命令)
 * ```
 *
 * ## 子步骤设计 (参考 bq25710 的 sub_step 机制)
 * 每个 demo 阶段内部有多个子步骤:
 * - ENROLL:  sub_step 0=等待手指, 1=采图, 2=生成特征, 3=等待抬起, ...
 * - VERIFY:  sub_step 0=等待手指, 1=采图, 2=生成特征, 3=搜索
 * - DELETE:  sub_step 0=读索引表, 1=删除目标, 2=验证删除
 */

#ifndef ZW101_FP_APP_H
#define ZW101_FP_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "zw101_fp_driver.h"
#include <stdint.h>
#include <stdbool.h>

/* ==================== 应用状态机枚举 ==================== */

/**
 * @brief 应用顶层任务状态
 */
typedef enum {
    ZW101_APP_STATE_INIT,            /**< 初始化: GPIO / 时钟 / 外设 */
    ZW101_APP_STATE_SELF_TEST,       /**< 自检: 握手 / 读参数 / 校验传感器 */
    ZW101_APP_STATE_DEMO_SELECT,     /**< 演示选择: 注册/验证/删除 */
    ZW101_APP_STATE_ENROLL_DEMO,     /**< 指纹注册演示 */
    ZW101_APP_STATE_VERIFY_DEMO,     /**< 指纹验证演示 */
    ZW101_APP_STATE_DELETE_DEMO,     /**< 指纹删除演示 */
    ZW101_APP_STATE_IDLE,            /**< 空闲: 等待用户命令 */
    ZW101_APP_STATE_TIMEOUT,         /**< 超时处理 */
    ZW101_APP_STATE_ERROR,           /**< 错误处理 */
} zw101_app_state_t;

/**
 * @brief Demo 类型选择
 */
typedef enum {
    ZW101_DEMO_NONE,
    ZW101_DEMO_ENROLL,
    ZW101_DEMO_VERIFY,
    ZW101_DEMO_DELETE,
    ZW101_DEMO_DELETE_ALL,
} zw101_demo_type_t;

/* ==================== 测试结果上下文 ==================== */

/**
 * @brief 阶段耗时记录 (参考 bq25710_stage_duration_t)
 */
typedef struct {
    uint32_t init_ms;            /**< 初始化耗时 */
    uint32_t self_test_ms;       /**< 自检耗时 */
    uint32_t enroll_ms;          /**< 注册耗时 */
    uint32_t verify_ms;          /**< 验证耗时 */
    uint32_t delete_ms;          /**< 删除耗时 */
} zw101_app_stage_time_t;

/**
 * @brief 自检结果位域 (参考 bq25710_test_result_t)
 */
typedef struct {
    uint8_t handshake_ok    : 1; /**< 握手通过 */
    uint8_t sys_para_ok     : 1; /**< 系统参数读取成功 */
    uint8_t sensor_ok       : 1; /**< 传感器校验通过 */
    uint8_t count_ok        : 1; /**< 模板数量读取成功 */
    uint8_t reserved        : 4;
} zw101_app_self_test_result_t;

/**
 * @brief 应用层全局上下文 (参考 bq25710_app_ctx_t)
 */
typedef struct {
    zw101_app_state_t   task_state;          /**< 当前任务状态 */
    zw101_app_state_t   last_state;          /**< 前一个状态 (用于错误恢复) */
    zw101_demo_type_t   demo_type;           /**< 当前演示类型 */
    uint32_t            state_entry_ms;      /**< 状态进入时间戳 */
    uint32_t            state_timeout_ms;    /**< 状态超时阈值 */
    uint8_t             sub_step;            /**< 子步骤编号 */
    uint32_t            sub_step_start_ms;   /**< 子步骤开始时间 */
    uint16_t            enroll_target_id;    /**< 注册/删除的目标 ID */
    uint8_t             enroll_count;        /**< 注册录入次数 */

    /* 结果记录 */
    zw101_app_self_test_result_t self_test_result;  /**< 自检结果 */
    zw101_app_stage_time_t       stage_time;        /**< 各阶段耗时 */
    zw101_drv_err_t              last_error;        /**< 最后一次错误码 */

    /* 驱动信息 */
    zw101_sys_para_t  sys_para;      /**< 模组系统参数 */
    zw101_add_para_t  add_para;      /**< 模组附加参数 */
    uint16_t          stored_count;  /**< 已存储指纹数 */

    /* 事件回调 */
    void (*notify_handler)(zw101_app_state_t state, uint8_t sub_step, zw101_drv_err_t err);
} zw101_app_ctx_t;

/* ==================== 公开接口 ==================== */

/**
 * @brief 初始化应用层
 *
 * 注册 HAL, 初始化 UART/GPIO/时钟, 设置默认参数。
 * 应在系统启动时调用。
 *
 * @param  ctx     应用上下文指针
 * @param  hal     HAL 回调接口 (静态/全局)
 * @param  handler 状态变化通知回调 (可为 NULL)
 * @return ZW101_DRV_OK 成功
 */
zw101_drv_err_t zw101_app_init(zw101_app_ctx_t *ctx, const zw101_hal_t *hal,
                               void (*handler)(zw101_app_state_t, uint8_t, zw101_drv_err_t));

/**
 * @brief 应用层主循环 (需在主循环或 RTOS 任务中周期性调用)
 *
 * 类似 bq25710_app_process(): 驱动状态机推进, 处理超时。
 *
 * @param  ctx 应用上下文指针
 * @return ZW101_DRV_OK 或无错误
 */
zw101_drv_err_t zw101_app_process(zw101_app_ctx_t *ctx);

/**
 * @brief 启动指定演示
 *
 * @param  ctx       应用上下文
 * @param  demo_type 演示类型
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_app_start_demo(zw101_app_ctx_t *ctx, zw101_demo_type_t demo_type);

/**
 * @brief 停止当前演示
 *
 * @param  ctx 应用上下文
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_app_stop_demo(zw101_app_ctx_t *ctx);

/**
 * @brief 获取状态名称 (调试用)
 *
 * @param  state 状态值
 * @return const char* 状态名称
 */
const char *zw101_app_state_name(zw101_app_state_t state);

/**
 * @brief 获取版本字符串
 * @return const char*
 */
const char *zw101_app_version(void);

#ifdef __cplusplus
}
#endif

#endif /* ZW101_FP_APP_H */
