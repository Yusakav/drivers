/**
 * @file    zw101_fp_app.c
 * @brief   ZW101 指纹模块应用层 — 演示程序实现
 * @note    参考 bq25710_app.c 架构:
 *          - 两阶段初始状态机 (INIT → SELF_TEST → IDLE)
 *          - 各 Demo 阶段独立处理函数 (process_state_xxx)
 *          - 状态切换自动记录时间戳 (参考 set_state_machine_state)
 *          - 超时自动进入 TIMEOUT 状态 (参考超时监控机制)
 *          - 子步骤设计 (sub_step) 用于长流程分步
 * @version 1.0
 * @date    2026-06-13
 */

#include "zw101_fp_app.h"
#include <string.h>

/* ==================== 版本 ==================== */

#define ZW101_APP_VERSION_MAJOR  1
#define ZW101_APP_VERSION_MINOR  0
#define ZW101_APP_VERSION_PATCH  0

/* ==================== 超时配置 ==================== */

#define APP_INIT_TIMEOUT_MS          5000u  /**< 初始化阶段总超时 */
#define APP_SELF_TEST_TIMEOUT_MS     8000u  /**< 自检阶段总超时 */
#define APP_ENROLL_TIMEOUT_MS        30000u /**< 注册演示总超时 (30s) */
#define APP_VERIFY_TIMEOUT_MS        15000u /**< 验证演示总超时 (15s) */
#define APP_DELETE_TIMEOUT_MS        5000u  /**< 删除演示总超时 */
#define APP_SUB_STEP_TIMEOUT_MS      5000u  /**< 单个子步骤超时 */

/* ==================== 全局 HAL 引用 (供回调使用) ==================== */

static const zw101_hal_t *g_app_hal = NULL;

/* ==================== 状态名称表 (调试用) ==================== */

static const char *g_state_names[] = {
    "INIT",
    "SELF_TEST",
    "DEMO_SELECT",
    "ENROLL_DEMO",
    "VERIFY_DEMO",
    "DELETE_DEMO",
    "IDLE",
    "TIMEOUT",
    "ERROR",
};

const char *zw101_app_state_name(zw101_app_state_t state)
{
    if (state <= ZW101_APP_STATE_ERROR) {
        return g_state_names[state];
    }
    return "UNKNOWN";
}

const char *zw101_app_version(void)
{
    static char ver[16];
    static bool cached = false;
    if (!cached) {
        snprintf(ver, sizeof(ver), "%d.%d.%d",
                 ZW101_APP_VERSION_MAJOR, ZW101_APP_VERSION_MINOR,
                 ZW101_APP_VERSION_PATCH);
        cached = true;
    }
    return ver;
}

/* ==================== 内部辅助函数 ==================== */

/**
 * @brief 设置状态机超时 (参考 bq25710 的机制)
 *
 * @param  ctx        应用上下文
 * @param  timeout_ms 超时阈值 (ms)
 */
static void set_state_timeout(zw101_app_ctx_t *ctx, uint32_t timeout_ms)
{
    ctx->state_timeout_ms = timeout_ms;
    ctx->state_entry_ms   = g_app_hal->get_tick_ms();
}

/**
 * @brief 切换状态机状态 (自动记录时间戳)
 *
 * 参考 bq25710 的状态切换逻辑: 保存 last_state, 更新时间, 清零 sub_step。
 *
 * @param  ctx        应用上下文
 * @param  new_state  新状态
 * @param  timeout_ms 新状态超时时间
 */
static void set_state(zw101_app_ctx_t *ctx, zw101_app_state_t new_state, uint32_t timeout_ms)
{
    ctx->last_state  = ctx->task_state;
    ctx->task_state  = new_state;
    ctx->sub_step    = 0;
    set_state_timeout(ctx, timeout_ms);
}

/**
 * @brief 进入错误状态
 *
 * @param  ctx   应用上下文
 * @param  error 错误码
 */
static void enter_error_state(zw101_app_ctx_t *ctx, zw101_drv_err_t error)
{
    ctx->last_error = error;
    set_state(ctx, ZW101_APP_STATE_ERROR, 0);
    if (ctx->notify_handler) {
        ctx->notify_handler(ZW101_APP_STATE_ERROR, 0, error);
    }
}

/**
 * @brief 检查状态超时
 *
 * @param  ctx 应用上下文
 * @return true=已超时
 */
static bool is_timeout(const zw101_app_ctx_t *ctx)
{
    if (ctx->state_timeout_ms == 0) return false;
    return (g_app_hal->get_tick_ms() - ctx->state_entry_ms) >= ctx->state_timeout_ms;
}

/* ==================== 注册事件回调 (桥接驱动层到应用层) ==================== */

static void on_enroll_event(zw101_enroll_stage_t stage, uint8_t progress,
                            uint16_t user_id, zw101_drv_err_t err, void *arg)
{
    zw101_app_ctx_t *ctx = (zw101_app_ctx_t *)arg;
    (void)user_id;

    if (err != ZW101_DRV_OK) {
        enter_error_state(ctx, err);
        return;
    }

    if (stage == ZW101_ENROLL_STAGE_DONE) {
        /* 注册成功, 返回 IDLE */
        ctx->enroll_target_id = user_id;
        set_state(ctx, ZW101_APP_STATE_IDLE, 0);
    }
}

/* ==================== 验证事件回调 ==================== */

static void on_verify_event(zw101_verify_stage_t stage, uint16_t user_id,
                            uint16_t score, zw101_drv_err_t err, void *arg)
{
    zw101_app_ctx_t *ctx = (zw101_app_ctx_t *)arg;
    (void)user_id;
    (void)score;

    if (err == ZW101_DRV_ERR_NOT_MATCH) {
        /* 验证不匹配: 回到 IDLE */
        set_state(ctx, ZW101_APP_STATE_IDLE, 0);
        return;
    }

    if (err != ZW101_DRV_OK) {
        enter_error_state(ctx, err);
        return;
    }

    if (stage == ZW101_VERIFY_STAGE_DONE) {
        set_state(ctx, ZW101_APP_STATE_IDLE, 0);
    }
}

/* ==================== 初始化 ==================== */

/**
 * @brief 初始化应用层
 *
 * 步骤:
 *   1. 保存 HAL 引用
 *   2. 初始化驱动层
 *   3. 清零上下文
 *   4. 进入 INIT 状态
 */
zw101_drv_err_t zw101_app_init(zw101_app_ctx_t *ctx, const zw101_hal_t *hal,
                               void (*handler)(zw101_app_state_t, uint8_t, zw101_drv_err_t))
{
    if (ctx == NULL || hal == NULL) return ZW101_DRV_ERR_PARAM;

    memset(ctx, 0, sizeof(zw101_app_ctx_t));
    g_app_hal           = hal;
    ctx->notify_handler = handler;
    ctx->enroll_count   = ZW101_DEFAULT_ENROLL_COUNT;

    /* 初始化驱动层 */
    zw101_drv_err_t ret = zw101_driver_init(hal);
    if (ret != ZW101_DRV_OK) {
        ctx->last_error = ret;
        return ret;
    }

    set_state(ctx, ZW101_APP_STATE_INIT, APP_INIT_TIMEOUT_MS);
    return ZW101_DRV_OK;
}

/* ==================== 各阶段处理函数 ==================== */

/**
 * @brief 处理 INIT 状态 (多子步骤)
 *
 * 子步骤:
 *   0: 上报 INIT 通知, 延时等待硬件稳定
 *   1: 模组上电
 *   2: 切换到 SELF_TEST
 */
static zw101_drv_err_t process_state_init(zw101_app_ctx_t *ctx)
{
    zw101_drv_err_t ret;

    switch (ctx->sub_step) {
    case 0:
        /* 等待硬件稳定 */
        g_app_hal->delay_ms(100);
        ctx->sub_step = 1;
        ctx->sub_step_start_ms = g_app_hal->get_tick_ms();
        break;

    case 1:
        /* 模组上电 */
        ret = zw101_power_on();
        if (ret != ZW101_DRV_OK) {
            enter_error_state(ctx, ret);
            return ret;
        }
        ctx->stage_time.init_ms = g_app_hal->get_tick_ms() - ctx->state_entry_ms;
        set_state(ctx, ZW101_APP_STATE_SELF_TEST, APP_SELF_TEST_TIMEOUT_MS);
        break;

    default:
        break;
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 处理 SELF_TEST 状态
 *
 * 子步骤:
 *   0: 执行自检
 *   1: 保存结果, 进入 IDLE
 */
static zw101_drv_err_t process_state_self_test(zw101_app_ctx_t *ctx)
{
    zw101_drv_err_t ret;

    switch (ctx->sub_step) {
    case 0:
        /* 执行自检 */
        ret = zw101_self_test(&ctx->sys_para, &ctx->add_para, &ctx->stored_count);
        if (ret != ZW101_DRV_OK) {
            enter_error_state(ctx, ret);
            return ret;
        }
        ctx->sub_step = 1;
        break;

    case 1:
        /* 自检通过, 标记所有结果 OK */
        ctx->self_test_result.handshake_ok = 1;
        ctx->self_test_result.sys_para_ok  = 1;
        ctx->self_test_result.sensor_ok    = 1;
        ctx->self_test_result.count_ok     = 1;
        ctx->stage_time.self_test_ms       = g_app_hal->get_tick_ms() - ctx->state_entry_ms;

        set_state(ctx, ZW101_APP_STATE_IDLE, 0);
        if (ctx->notify_handler) {
            ctx->notify_handler(ZW101_APP_STATE_IDLE, 0, ZW101_DRV_OK);
        }
        break;

    default:
        break;
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 处理 ENROLL_DEMO 状态
 *
 * 通过调用驱动层的 zw101_enroll_process() 推进注册。
 *
 * 子步骤:
 *   0: 启动注册 (调用 zw101_enroll_start)
 *   1: 轮询 zw101_enroll_process() 直到完成
 *
 * 驱动层的 enroll 状态机是异步的, 应用层只需周期性调用 process()。
 */
static zw101_drv_err_t process_state_enroll_demo(zw101_app_ctx_t *ctx)
{
    zw101_drv_err_t ret;

    switch (ctx->sub_step) {
    case 0:
        /* 启动注册 */
        ret = zw101_enroll_start(ctx->enroll_count, on_enroll_event, ctx);
        if (ret != ZW101_DRV_OK) {
            enter_error_state(ctx, ret);
            return ret;
        }
        ctx->sub_step = 1;
        ctx->sub_step_start_ms = g_app_hal->get_tick_ms();
        break;

    case 1:
        /* 轮询驱动层状态机 */
        ret = zw101_enroll_process();
        if (ret == ZW101_DRV_OK) {
            /* 仍在进行中, 检查子步骤超时 */
            if ((g_app_hal->get_tick_ms() - ctx->sub_step_start_ms) > APP_SUB_STEP_TIMEOUT_MS) {
                /* 子步骤超时 → 取消注册 → 进入 TIMEOUT */
                zw101_enroll_cancel();
                enter_error_state(ctx, ZW101_DRV_ERR_TIMEOUT);
                return ZW101_DRV_ERR_TIMEOUT;
            }
        } else {
            /* 注册出错, 回调已处理 */
            enter_error_state(ctx, ret);
            return ret;
        }
        /* 如果驱动层已完成, 回调会在 on_enroll_event 中 set_state(IDLE) */
        break;

    default:
        break;
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 处理 VERIFY_DEMO 状态
 *
 * 通过调用驱动层的 zw101_verify_process() 推进验证。
 *
 * 子步骤:
 *   0: 启动验证
 *   1: 轮询 zw101_verify_process() 直到完成
 */
static zw101_drv_err_t process_state_verify_demo(zw101_app_ctx_t *ctx)
{
    zw101_drv_err_t ret;

    switch (ctx->sub_step) {
    case 0:
        ret = zw101_verify_start(on_verify_event, ctx);
        if (ret != ZW101_DRV_OK) {
            enter_error_state(ctx, ret);
            return ret;
        }
        ctx->sub_step = 1;
        ctx->sub_step_start_ms = g_app_hal->get_tick_ms();
        break;

    case 1:
        ret = zw101_verify_process();
        if (ret == ZW101_DRV_OK) {
            /* 仍在进行中 */
            if ((g_app_hal->get_tick_ms() - ctx->sub_step_start_ms) > APP_SUB_STEP_TIMEOUT_MS) {
                zw101_verify_cancel();
                enter_error_state(ctx, ZW101_DRV_ERR_TIMEOUT);
                return ZW101_DRV_ERR_TIMEOUT;
            }
        } else {
            enter_error_state(ctx, ret);
            return ret;
        }
        break;

    default:
        break;
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 处理 DELETE_DEMO 状态
 *
 * 子步骤:
 *   0: 读有效模板数
 *   1: 执行删除 (按 ID 删除或全部清空)
 *   2: 验证删除结果
 */
static zw101_drv_err_t process_state_delete_demo(zw101_app_ctx_t *ctx)
{
    zw101_drv_err_t ret;

    switch (ctx->sub_step) {
    case 0:
        /* 读取当前模板数 */
        ret = zw101_get_stored_count(&ctx->stored_count);
        if (ret != ZW101_DRV_OK) {
            enter_error_state(ctx, ret);
            return ret;
        }
        ctx->sub_step = 1;
        break;

    case 1:
        /* 执行删除 */
        if (ctx->demo_type == ZW101_DEMO_DELETE_ALL) {
            ret = zw101_delete_all();
        } else {
            ret = zw101_delete_finger(ctx->enroll_target_id);
        }
        if (ret != ZW101_DRV_OK) {
            enter_error_state(ctx, ret);
            return ret;
        }
        ctx->sub_step = 2;
        ctx->sub_step_start_ms = g_app_hal->get_tick_ms();
        break;

    case 2:
        /* 验证删除结果 */
        {
            uint16_t new_count;
            ret = zw101_get_stored_count(&new_count);
            if (ret != ZW101_DRV_OK) {
                enter_error_state(ctx, ret);
                return ret;
            }
            ctx->stored_count = new_count;
        }
        ctx->stage_time.delete_ms = g_app_hal->get_tick_ms() - ctx->state_entry_ms;
        set_state(ctx, ZW101_APP_STATE_IDLE, 0);
        if (ctx->notify_handler) {
            ctx->notify_handler(ZW101_APP_STATE_IDLE, 0, ZW101_DRV_OK);
        }
        break;

    default:
        break;
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 处理 TIMEOUT 状态
 */
static zw101_drv_err_t process_state_timeout(zw101_app_ctx_t *ctx)
{
    /* 取消当前操作, 回到 IDLE */
    zw101_enroll_cancel();
    zw101_verify_cancel();

    ctx->last_error = ZW101_DRV_ERR_TIMEOUT;
    set_state(ctx, ZW101_APP_STATE_IDLE, 0);
    if (ctx->notify_handler) {
        ctx->notify_handler(ZW101_APP_STATE_TIMEOUT, 0, ZW101_DRV_ERR_TIMEOUT);
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 处理 ERROR 状态
 */
static zw101_drv_err_t process_state_error(zw101_app_ctx_t *ctx)
{
    /* 清理当前操作 */
    zw101_enroll_cancel();
    zw101_verify_cancel();

    /* 尝试恢复: 重新上电 + 自检 */
    zw101_power_off();
    g_app_hal->delay_ms(200);
    zw101_power_on();

    zw101_drv_err_t ret = zw101_self_test(&ctx->sys_para, &ctx->add_para, &ctx->stored_count);
    if (ret == ZW101_DRV_OK) {
        /* 恢复成功 */
        set_state(ctx, ZW101_APP_STATE_IDLE, 0);
    } else {
        /* 恢复失败, 继续留在 ERROR 状态 */
        ctx->last_error = ret;
    }

    return ret;
}

/**
 * @brief 处理 IDLE 状态
 *
 * 空闲状态: 周期性更新 TOUCH_OUT 状态或等待命令。
 */
static zw101_drv_err_t process_state_idle(zw101_app_ctx_t *ctx)
{
    (void)ctx;
    /* IDLE 状态不做任何操作, 等待外部命令 (zw101_app_start_demo) */
    return ZW101_DRV_OK;
}

/* ==================== 主循环 ==================== */

/**
 * @brief 应用层主循环
 *
 * 类似 bq25710 的 process() 函数:
 *   1. 检查状态超时
 *   2. 按当前状态分派处理函数
 *
 * @param  ctx 应用上下文
 * @return ZW101_DRV_OK 或无错误
 */
zw101_drv_err_t zw101_app_process(zw101_app_ctx_t *ctx)
{
    if (ctx == NULL || g_app_hal == NULL) return ZW101_DRV_ERR_PARAM;

    /* 超时监控 (IDLE 和 ERROR 状态不设超时) */
    if (ctx->task_state != ZW101_APP_STATE_IDLE &&
        ctx->task_state != ZW101_APP_STATE_ERROR) {
        if (is_timeout(ctx)) {
            set_state(ctx, ZW101_APP_STATE_TIMEOUT, 0);
        }
    }

    /* 状态分派 */
    switch (ctx->task_state) {
    case ZW101_APP_STATE_INIT:
        return process_state_init(ctx);

    case ZW101_APP_STATE_SELF_TEST:
        return process_state_self_test(ctx);

    case ZW101_APP_STATE_ENROLL_DEMO:
        return process_state_enroll_demo(ctx);

    case ZW101_APP_STATE_VERIFY_DEMO:
        return process_state_verify_demo(ctx);

    case ZW101_APP_STATE_DELETE_DEMO:
        return process_state_delete_demo(ctx);

    case ZW101_APP_STATE_TIMEOUT:
        return process_state_timeout(ctx);

    case ZW101_APP_STATE_ERROR:
        return process_state_error(ctx);

    case ZW101_APP_STATE_IDLE:
    default:
        return process_state_idle(ctx);
    }
}

/* ==================== Demo 启动接口 ==================== */

/**
 * @brief 启动指定演示
 */
zw101_drv_err_t zw101_app_start_demo(zw101_app_ctx_t *ctx, zw101_demo_type_t demo_type)
{
    if (ctx == NULL) return ZW101_DRV_ERR_PARAM;
    if (ctx->task_state != ZW101_APP_STATE_IDLE) return ZW101_DRV_ERR_BUSY;

    ctx->demo_type = demo_type;

    switch (demo_type) {
    case ZW101_DEMO_ENROLL:
        set_state(ctx, ZW101_APP_STATE_ENROLL_DEMO, APP_ENROLL_TIMEOUT_MS);
        break;

    case ZW101_DEMO_VERIFY:
        set_state(ctx, ZW101_APP_STATE_VERIFY_DEMO, APP_VERIFY_TIMEOUT_MS);
        break;

    case ZW101_DEMO_DELETE:
    case ZW101_DEMO_DELETE_ALL:
        set_state(ctx, ZW101_APP_STATE_DELETE_DEMO, APP_DELETE_TIMEOUT_MS);
        break;

    default:
        return ZW101_DRV_ERR_PARAM;
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 停止当前演示
 */
zw101_drv_err_t zw101_app_stop_demo(zw101_app_ctx_t *ctx)
{
    if (ctx == NULL) return ZW101_DRV_ERR_PARAM;

    zw101_enroll_cancel();
    zw101_verify_cancel();

    set_state(ctx, ZW101_APP_STATE_IDLE, 0);
    return ZW101_DRV_OK;
}
