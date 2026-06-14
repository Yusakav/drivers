/**
 * @file    zw101_fp_driver.c
 * @brief   ZW101 指纹模块驱动层 — 高层操作封装实现
 * @note    基于协议层 zw101_fp_protocol.c，所有 UART 通信均通过 HAL 回调
 * @version 1.0
 * @date    2026-06-13
 *
 * ## 超时与重试设计
 * - 通用指令: 1000ms 超时, 不重试
 * - 采图指令: 480ms 超时/次, 最多重试 3 次
 * - 搜索指令: 2300ms 超时, 不重试
 * - 注册流程: 每帧采图 + 生成特征 最多各重试 3 次
 *
 * ## 状态机设计
 * - 注册: IDLE → WAIT_FINGER → CAPTURING → EXTRACTING → WAIT_RELEASE
 *          → (循环) → MERGING → STORING → DONE/FAILED
 * - 验证: IDLE → WAIT_FINGER → CAPTURING → EXTRACTING → SEARCHING → DONE/FAILED
 */

#include "zw101_fp_driver.h"
#include <string.h>

/* ==================== 驱动版本 ==================== */

#define ZW101_DRV_VERSION_MAJOR  1
#define ZW101_DRV_VERSION_MINOR  0
#define ZW101_DRV_VERSION_PATCH  0

/* ==================== 驱动内部状态枚举 ==================== */

typedef enum {
    ZW101_STATE_IDLE,
    ZW101_STATE_POWER_ON,
    ZW101_STATE_SELF_TEST,
    ZW101_STATE_ENROLLING,
    ZW101_STATE_VERIFYING,
    ZW101_STATE_CMD_PENDING,   /**< 执行一次性命令 (删除/清空/LED/休眠等) */
} zw101_internal_state_t;

/* ==================== 驱动上下文 ==================== */

typedef struct {
    const zw101_hal_t *hal;                   /**< HAL 接口 */
    zw101_internal_state_t state;             /**< 当前内部状态 */
    uint8_t             last_ack;             /**< 最后一次模组确认码 */
    bool                initialized;          /**< 驱动是否已初始化 */

    /* 接收缓冲区 */
    uint8_t  rcv_buf[ZW101_RCV_BUF_MAX];
    uint16_t rcv_len;

    /* 注册上下文 */
    struct {
        uint8_t              target_count;    /**< 目标录入次数 */
        uint8_t              current_try;     /**< 当前帧编号 (1-based) */
        uint8_t              capture_retry;   /**< 采图重试计数 */
        uint8_t              extract_retry;   /**< 生成特征重试计数 */
        uint8_t              progress;        /**< 进度百分比 */
        zw101_enroll_stage_t stage;
        zw101_enroll_cb_t    cb;
        void                *cb_arg;
        uint16_t             allocated_id;    /**< 分配的指纹 ID */
    } enroll;

    /* 验证上下文 */
    struct {
        bool                 is_identify;     /**< true=1:1, false=1:N */
        uint16_t             target_id;       /**< 1:1 比对的目标 ID */
        zw101_verify_stage_t stage;
        zw101_verify_cb_t    cb;
        void                *cb_arg;
        uint16_t             matched_id;
        uint16_t             match_score;
    } verify;
} zw101_context_t;

/* 全局驱动实例 (单例) */
static zw101_context_t g_ctx;

/* ==================== 内部函数声明 ==================== */

static zw101_drv_err_t send_cmd_and_wait_ack(const uint8_t *cmd, uint16_t cmd_len,
                                             uint32_t timeout_ms);
static zw101_drv_err_t send_cmd_get_image(bool is_enroll);
static zw101_drv_err_t send_cmd_gen_char(uint8_t buffer_id);
static zw101_drv_err_t send_cmd_store(uint16_t buffer_id, uint16_t page_id);
static zw101_drv_err_t send_cmd_search(uint8_t buffer_id, uint16_t start_id, uint16_t count);
static zw101_drv_err_t send_cmd_match(uint8_t buffer_id);
static zw101_drv_err_t send_cmd_reg_model(void);
static zw101_drv_err_t send_cmd_delete(uint16_t page_id);
static zw101_drv_err_t send_cmd_empty(void);
static zw101_drv_err_t send_cmd_sleep(void);
static zw101_drv_err_t send_cmd_handshake(void);
static zw101_drv_err_t send_cmd_check_sensor(void);
static zw101_drv_err_t send_cmd_rgb(uint8_t func, uint8_t color,
                                    uint8_t end_duty, uint8_t loop_times, uint8_t cycle);
static int32_t parse_ack_from_response(const uint8_t *data, uint16_t len);

/* ==================== 初始化 ==================== */

/**
 * @brief 初始化 ZW101 驱动层
 *
 * 注册 HAL 回调，将驱动状态重置为 IDLE。
 *
 * @param  hal HAL 接口指针
 * @return zw101_drv_err_t
 */
zw101_drv_err_t zw101_driver_init(const zw101_hal_t *hal)
{
    if (hal == NULL) {
        return ZW101_DRV_ERR_PARAM;
    }
    if (hal->uart_send == NULL || hal->uart_recv == NULL ||
        hal->delay_ms == NULL || hal->get_tick_ms == NULL) {
        return ZW101_DRV_ERR_PARAM;
    }

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.hal         = hal;
    g_ctx.state       = ZW101_STATE_IDLE;
    g_ctx.initialized = true;

    return ZW101_DRV_OK;
}

/**
 * @brief 模组上电并等待初始化完成
 *
 * 流程: VCC 上电 → 等待 ZW101_PWRON_WAIT_MS → 检查握手信号 0x55
 *
 * @return ZW101_DRV_OK 或超时/硬件错误
 */
zw101_drv_err_t zw101_power_on(void)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;

    g_ctx.hal->power_ctrl(true);
    g_ctx.hal->delay_ms(ZW101_PWRON_WAIT_MS);

    /* 模组上电后通过 UART 发送 0x55 握手指示 */
    uint32_t start = g_ctx.hal->get_tick_ms();
    while ((g_ctx.hal->get_tick_ms() - start) < ZW101_TIMEOUT_SELF_TEST) {
        uint16_t n = g_ctx.hal->uart_recv(g_ctx.rcv_buf, 1, 100);
        if (n > 0 && g_ctx.rcv_buf[0] == 0x55) {
            g_ctx.state = ZW101_STATE_POWER_ON;
            return ZW101_DRV_OK;
        }
    }

    return ZW101_DRV_ERR_TIMEOUT;
}

/**
 * @brief 模组断电
 */
zw101_drv_err_t zw101_power_off(void)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;

    g_ctx.hal->power_ctrl(false);
    g_ctx.state = ZW101_STATE_IDLE;

    return ZW101_DRV_OK;
}

/* ==================== 自检 ==================== */

/**
 * @brief 模组自检流程
 *
 * 1. 握手 (PS_HandShake)
 * 2. 读基本参数 (PS_ReadSysPara)
 * 3. 读附加参数 (PS_ReadAddPara)
 * 4. 校验传感器 (PS_CheckSensor)
 * 5. 读有效模板数 (PS_ValidTempleteNum)
 *
 * @return ZW101_DRV_OK 全部通过
 */
zw101_drv_err_t zw101_self_test(zw101_sys_para_t *para_out,
                                zw101_add_para_t *add_para_out,
                                uint16_t *stored_count_out)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;

    zw101_drv_err_t ret;

    /* 步骤 1: 握手 */
    ret = send_cmd_handshake();
    if (ret != ZW101_DRV_OK) return ret;

    /* 步骤 2: 读基本参数 */
    {
        uint8_t cmd_buf[32];
        uint16_t cmd_len = zw101_build_cmd_pkg(cmd_buf, ZW101_DEFAULT_DEV_ADDR,
                                                ZW101_CMD_READ_SYS_PARA, NULL, 0);
        ret = send_cmd_and_wait_ack(cmd_buf, cmd_len, ZW101_TIMEOUT_COMMON);
        if (ret != ZW101_DRV_OK) return ret;

        if (para_out != NULL) {
            /* 应答格式: ack(1) + 16bytes para + checksum(2) — total payload = 19 */
            zw101_parse_sys_para(g_ctx.rcv_buf + 1, 19, para_out);
        }
    }

    /* 步骤 3: 读附加参数 */
    if (add_para_out != NULL) {
        uint8_t cmd_buf[32];
        uint16_t cmd_len = zw101_build_cmd_pkg(cmd_buf, ZW101_DEFAULT_DEV_ADDR,
                                                ZW101_CMD_READ_ADD_PARA, NULL, 0);
        ret = send_cmd_and_wait_ack(cmd_buf, cmd_len, ZW101_TIMEOUT_COMMON);
        if (ret != ZW101_DRV_OK) return ret;

        add_para_out->sensor_width  = ((uint16_t)g_ctx.rcv_buf[1] << 8) | g_ctx.rcv_buf[2];
        add_para_out->sensor_height = ((uint16_t)g_ctx.rcv_buf[3] << 8) | g_ctx.rcv_buf[4];
        add_para_out->led_type      = ((uint16_t)g_ctx.rcv_buf[5] << 8) | g_ctx.rcv_buf[6];
        add_para_out->tmp_per_fp    = ((uint16_t)g_ctx.rcv_buf[7] << 8) | g_ctx.rcv_buf[8];
    }

    /* 步骤 4: 校验传感器 */
    ret = send_cmd_check_sensor();
    if (ret != ZW101_DRV_OK) return ret;

    /* 步骤 5: 读有效模板数 */
    {
        uint8_t cmd_buf[32];
        uint16_t cmd_len = zw101_build_cmd_pkg(cmd_buf, ZW101_DEFAULT_DEV_ADDR,
                                                ZW101_CMD_VALID_TEMPLATE_NUM, NULL, 0);
        ret = send_cmd_and_wait_ack(cmd_buf, cmd_len, ZW101_TIMEOUT_COMMON);
        if (ret != ZW101_DRV_OK) return ret;

        if (stored_count_out != NULL) {
            *stored_count_out = ((uint16_t)g_ctx.rcv_buf[1] << 8) | g_ctx.rcv_buf[2];
        }
    }

    g_ctx.state = ZW101_STATE_IDLE;
    return ZW101_DRV_OK;
}

/* ==================== 指纹录入 ==================== */

/**
 * @brief 开始指纹录入流程
 *
 * 注册状态机: 采用模组底层的单步指令逐帧完成
 * (非自动注册 PS_AutoEnroll, 以获得更细粒度的控制)
 *
 * @param  enroll_count 录入次数, 0=使用默认值
 * @param  cb           阶段回调
 * @param  arg          用户参数
 * @return ZW101_DRV_OK 启动成功
 */
zw101_drv_err_t zw101_enroll_start(uint8_t enroll_count,
                                   zw101_enroll_cb_t cb, void *arg)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;
    if (g_ctx.state != ZW101_STATE_IDLE) return ZW101_DRV_ERR_BUSY;

    /* 检查指纹库是否已满 */
    uint16_t count;
    zw101_drv_err_t ret = zw101_get_stored_count(&count);
    if (ret != ZW101_DRV_OK) return ret;
    if (count >= ZW101_MAX_FINGERPRINT_COUNT) return ZW101_DRV_ERR_LIB_FULL;

    /* 寻找空闲 ID */
    uint16_t free_id;
    ret = zw101_find_free_id(&free_id);
    if (ret != ZW101_DRV_OK) return ret;

    g_ctx.enroll.target_count  = (enroll_count > 0) ? enroll_count : ZW101_DEFAULT_ENROLL_COUNT;
    g_ctx.enroll.current_try   = 1;
    g_ctx.enroll.capture_retry = 0;
    g_ctx.enroll.extract_retry = 0;
    g_ctx.enroll.progress      = 0;
    g_ctx.enroll.stage         = ZW101_ENROLL_STAGE_WAIT_FINGER;
    g_ctx.enroll.cb            = cb;
    g_ctx.enroll.cb_arg        = arg;
    g_ctx.enroll.allocated_id  = free_id;

    g_ctx.state = ZW101_STATE_ENROLLING;

    if (cb) {
        cb(ZW101_ENROLL_STAGE_WAIT_FINGER, 0, free_id, ZW101_DRV_OK, arg);
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 推进注册状态机
 *
 * 主控应在主循环中周期性调用此函数。
 *
 * 流程:
 *   WAIT_FINGER → 轮询 TOUCH_OUT / 发送采图指令
 *   CAPTURING   → 等待采图应答
 *   EXTRACTING  → 生成特征 (BufferID=1 用于第一帧, BufferID=2 用于后续帧)
 *   WAIT_RELEASE→ 提示用户抬手指
 *   (循环直到采集足够帧数)
 *   MERGING     → 发送 PS_RegModel 合并特征
 *   STORING     → 发送 PS_StoreChar 存储模板
 *   DONE        → 回调通知
 *
 * @return zw101_drv_err_t
 */
zw101_drv_err_t zw101_enroll_process(void)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;
    if (g_ctx.state != ZW101_STATE_ENROLLING) return ZW101_DRV_ERR_BUSY;

    zw101_drv_err_t ret;

    switch (g_ctx.enroll.stage) {

    /* ---------- 等待手指 ---------- */
    case ZW101_ENROLL_STAGE_WAIT_FINGER:
        /* 检查 TOUCH_OUT 信号 */
        if (g_ctx.hal->read_touch_out != NULL && g_ctx.hal->read_touch_out()) {
            /* 手指已触摸, 开始采图 */
            g_ctx.enroll.stage = ZW101_ENROLL_STAGE_CAPTURING;
            ret = send_cmd_get_image(true); /* 注册用采图 (0x29) */
            if (ret != ZW101_DRV_OK) {
                g_ctx.enroll.stage = ZW101_ENROLL_STAGE_FAILED;
                if (g_ctx.enroll.cb) {
                    g_ctx.enroll.cb(ZW101_ENROLL_STAGE_FAILED, g_ctx.enroll.progress,
                                    0, ret, g_ctx.enroll.cb_arg);
                }
                return ret;
            }
            /* 采图指令已发送, 下一轮 process 将收到应答 */
            g_ctx.enroll.capture_retry = 0;
        }
        break;

    /* ---------- 采图中 (等待应答) ---------- */
    case ZW101_ENROLL_STAGE_CAPTURING:
        {
            int32_t ack = g_ctx.last_ack;
            if (ack == ZW101_ACK_SUCCESS) {
                /* 采图成功 → 生成特征 (BufferID=1 第一帧, 之后用 BufferID=2) */
                g_ctx.enroll.stage = ZW101_ENROLL_STAGE_EXTRACTING;
                uint8_t buf_id = (g_ctx.enroll.current_try == 1) ? 1 : 2;
                ret = send_cmd_gen_char(buf_id);
                if (ret != ZW101_DRV_OK) {
                    g_ctx.enroll.stage = ZW101_ENROLL_STAGE_FAILED;
                    goto enroll_fail;
                }
                g_ctx.enroll.extract_retry = 0;
            } else if (ack == ZW101_ACK_ERR_NO_FINGER) {
                /* 无手指: 回到等待状态 (正常情况, 不消耗重试) */
                g_ctx.enroll.stage = ZW101_ENROLL_STAGE_WAIT_FINGER;
            } else {
                /* 采图失败, 重试 */
                g_ctx.enroll.capture_retry++;
                if (g_ctx.enroll.capture_retry < ZW101_CAPTURE_MAX_RETRY) {
                    ret = send_cmd_get_image(true);
                    if (ret != ZW101_DRV_OK) {
                        g_ctx.enroll.stage = ZW101_ENROLL_STAGE_FAILED;
                        goto enroll_fail;
                    }
                } else {
                    g_ctx.enroll.stage = ZW101_ENROLL_STAGE_FAILED;
                    ret = ZW101_DRV_ERR_MODULE;
                    goto enroll_fail;
                }
            }
        }
        break;

    /* ---------- 生成特征中 (等待应答) ---------- */
    case ZW101_ENROLL_STAGE_EXTRACTING:
        {
            int32_t ack = g_ctx.last_ack;
            if (ack == ZW101_ACK_SUCCESS) {
                /* 特征生成成功 */
                if (g_ctx.enroll.current_try < g_ctx.enroll.target_count) {
                    /* 还有帧没采完 → 提示抬手指 */
                    g_ctx.enroll.stage = ZW101_ENROLL_STAGE_WAIT_RELEASE;
                    g_ctx.enroll.progress = (uint8_t)((uint16_t)g_ctx.enroll.current_try * 100u
                                                      / g_ctx.enroll.target_count);
                    if (g_ctx.enroll.cb) {
                        g_ctx.enroll.cb(ZW101_ENROLL_STAGE_WAIT_RELEASE,
                                        g_ctx.enroll.progress, 0, ZW101_DRV_OK,
                                        g_ctx.enroll.cb_arg);
                    }
                } else {
                    /* 所有帧采完 → 合并模板 */
                    g_ctx.enroll.stage = ZW101_ENROLL_STAGE_MERGING;
                    g_ctx.enroll.progress = 80;
                    ret = send_cmd_reg_model();
                    if (ret != ZW101_DRV_OK) {
                        g_ctx.enroll.stage = ZW101_ENROLL_STAGE_FAILED;
                        goto enroll_fail;
                    }
                }
            } else {
                /* 生成特征失败, 重试采图 */
                g_ctx.enroll.extract_retry++;
                if (g_ctx.enroll.extract_retry < ZW101_EXTRACT_MAX_RETRY) {
                    g_ctx.enroll.stage = ZW101_ENROLL_STAGE_CAPTURING;
                    ret = send_cmd_get_image(true);
                    if (ret != ZW101_DRV_OK) {
                        g_ctx.enroll.stage = ZW101_ENROLL_STAGE_FAILED;
                        goto enroll_fail;
                    }
                } else {
                    g_ctx.enroll.stage = ZW101_ENROLL_STAGE_FAILED;
                    ret = ZW101_DRV_ERR_MODULE;
                    goto enroll_fail;
                }
            }
        }
        break;

    /* ---------- 等待手指抬起 ---------- */
    case ZW101_ENROLL_STAGE_WAIT_RELEASE:
        /* 检查 TOUCH_OUT: 手指抬起后进入下一帧 */
        if (g_ctx.hal->read_touch_out != NULL && !g_ctx.hal->read_touch_out()) {
            g_ctx.enroll.current_try++;
            g_ctx.enroll.stage = ZW101_ENROLL_STAGE_WAIT_FINGER;
            if (g_ctx.enroll.cb) {
                g_ctx.enroll.cb(ZW101_ENROLL_STAGE_WAIT_FINGER,
                                g_ctx.enroll.progress, 0, ZW101_DRV_OK,
                                g_ctx.enroll.cb_arg);
            }
        }
        break;

    /* ---------- 合并模板中 (等待应答) ---------- */
    case ZW101_ENROLL_STAGE_MERGING:
        if (g_ctx.last_ack == ZW101_ACK_SUCCESS) {
            /* 合并成功 → 存储到指纹库 */
            g_ctx.enroll.stage = ZW101_ENROLL_STAGE_STORING;
            g_ctx.enroll.progress = 90;
            ret = send_cmd_store(1, g_ctx.enroll.allocated_id);
            if (ret != ZW101_DRV_OK) {
                g_ctx.enroll.stage = ZW101_ENROLL_STAGE_FAILED;
                goto enroll_fail;
            }
        } else {
            /* 合并失败 */
            g_ctx.enroll.stage = ZW101_ENROLL_STAGE_FAILED;
            ret = ZW101_DRV_ERR_MODULE;
            goto enroll_fail;
        }
        break;

    /* ---------- 存储模板中 (等待应答) ---------- */
    case ZW101_ENROLL_STAGE_STORING:
        if (g_ctx.last_ack == ZW101_ACK_SUCCESS) {
            /* 存储成功 → 完成 */
            g_ctx.enroll.stage   = ZW101_ENROLL_STAGE_DONE;
            g_ctx.enroll.progress = 100;
            g_ctx.state          = ZW101_STATE_IDLE;
            zw101_rgb_success();
            zw101_beep(200);
            if (g_ctx.enroll.cb) {
                g_ctx.enroll.cb(ZW101_ENROLL_STAGE_DONE, 100,
                                g_ctx.enroll.allocated_id, ZW101_DRV_OK,
                                g_ctx.enroll.cb_arg);
            }
        } else {
            g_ctx.enroll.stage = ZW101_ENROLL_STAGE_FAILED;
            ret = ZW101_DRV_ERR_MODULE;
            goto enroll_fail;
        }
        break;

    /* ---------- 注册失败 ---------- */
    enroll_fail:
    case ZW101_ENROLL_STAGE_FAILED:
        g_ctx.enroll.stage = ZW101_ENROLL_STAGE_FAILED;
        g_ctx.state = ZW101_STATE_IDLE;
        zw101_rgb_failure();
        zw101_beep(100);
        if (g_ctx.enroll.cb) {
            g_ctx.enroll.cb(ZW101_ENROLL_STAGE_FAILED, g_ctx.enroll.progress,
                            0, ret, g_ctx.enroll.cb_arg);
        }
        return ret;

    default:
        break;
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 取消正在进行的注册
 */
zw101_drv_err_t zw101_enroll_cancel(void)
{
    if (g_ctx.state != ZW101_STATE_ENROLLING) return ZW101_DRV_ERR_BUSY;

    /* 发送取消指令 */
    uint8_t cmd[32];
    uint16_t cmd_len = zw101_build_cmd_pkg(cmd, ZW101_DEFAULT_DEV_ADDR,
                                            ZW101_CMD_CANCEL, NULL, 0);
    send_cmd_and_wait_ack(cmd, cmd_len, ZW101_TIMEOUT_COMMON);

    g_ctx.enroll.stage = ZW101_ENROLL_STAGE_IDLE;
    g_ctx.state = ZW101_STATE_IDLE;
    if (g_ctx.enroll.cb) {
        g_ctx.enroll.cb(ZW101_ENROLL_STAGE_FAILED, 0, 0,
                        ZW101_DRV_ERR_CANCELED, g_ctx.enroll.cb_arg);
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 获取注册进度
 */
zw101_drv_err_t zw101_enroll_get_progress(zw101_enroll_stage_t *stage_out,
                                          uint8_t *progress_out)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;

    if (stage_out)   *stage_out   = g_ctx.enroll.stage;
    if (progress_out) *progress_out = g_ctx.enroll.progress;

    return ZW101_DRV_OK;
}

/* ==================== 指纹验证 / 搜索 ==================== */

/**
 * @brief 开始 1:N 指纹验证 (搜索模式)
 *
 * 状态机流程:
 *   IDLE → WAIT_FINGER → CAPTURING → EXTRACTING → SEARCHING → DONE/FAILED
 *
 * @param  cb   阶段回调
 * @param  arg  用户参数
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_verify_start(zw101_verify_cb_t cb, void *arg)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;
    if (g_ctx.state != ZW101_STATE_IDLE) return ZW101_DRV_ERR_BUSY;

    g_ctx.verify.is_identify = false;
    g_ctx.verify.target_id   = 0;
    g_ctx.verify.stage       = ZW101_VERIFY_STAGE_WAIT_FINGER;
    g_ctx.verify.cb          = cb;
    g_ctx.verify.cb_arg      = arg;

    g_ctx.state = ZW101_STATE_VERIFYING;

    if (cb) {
        cb(ZW101_VERIFY_STAGE_WAIT_FINGER, 0, 0, ZW101_DRV_OK, arg);
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 开始 1:1 指纹比对
 *
 * @param  target_id 目标 ID
 * @param  cb        阶段回调
 * @param  arg       用户参数
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_identify_start(uint16_t target_id,
                                     zw101_verify_cb_t cb, void *arg)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;
    if (g_ctx.state != ZW101_STATE_IDLE) return ZW101_DRV_ERR_BUSY;

    g_ctx.verify.is_identify = true;
    g_ctx.verify.target_id   = target_id;
    g_ctx.verify.stage       = ZW101_VERIFY_STAGE_WAIT_FINGER;
    g_ctx.verify.cb          = cb;
    g_ctx.verify.cb_arg      = arg;

    g_ctx.state = ZW101_STATE_VERIFYING;

    if (cb) {
        cb(ZW101_VERIFY_STAGE_WAIT_FINGER, 0, 0, ZW101_DRV_OK, arg);
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 推进验证状态机
 *
 * 流程:
 *   WAIT_FINGER → 轮询 TOUCH_OUT / 发送采图指令
 *   CAPTURING   → 等待采图应答
 *   EXTRACTING  → 生成特征 (BufferID=1)
 *   SEARCHING   → 1:N 搜索或 1:1 比对
 *   DONE        → 回调通知
 *
 * @return zw101_drv_err_t
 */
zw101_drv_err_t zw101_verify_process(void)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;
    if (g_ctx.state != ZW101_STATE_VERIFYING) return ZW101_DRV_ERR_BUSY;

    zw101_drv_err_t ret;

    switch (g_ctx.verify.stage) {

    /* ---------- 等待手指 ---------- */
    case ZW101_VERIFY_STAGE_WAIT_FINGER:
        if (g_ctx.hal->read_touch_out != NULL && g_ctx.hal->read_touch_out()) {
            g_ctx.verify.stage = ZW101_VERIFY_STAGE_CAPTURING;
            ret = send_cmd_get_image(false); /* 验证用采图 (0x01) */
            if (ret != ZW101_DRV_OK) {
                g_ctx.verify.stage = ZW101_VERIFY_STAGE_FAILED;
                if (g_ctx.verify.cb) {
                    g_ctx.verify.cb(ZW101_VERIFY_STAGE_FAILED, 0, 0, ret,
                                    g_ctx.verify.cb_arg);
                }
                return ret;
            }
        }
        break;

    /* ---------- 采图中 ---------- */
    case ZW101_VERIFY_STAGE_CAPTURING:
        if (g_ctx.last_ack == ZW101_ACK_SUCCESS) {
            /* 采图成功 → 生成特征 */
            g_ctx.verify.stage = ZW101_VERIFY_STAGE_EXTRACTING;
            ret = send_cmd_gen_char(1);
            if (ret != ZW101_DRV_OK) {
                g_ctx.verify.stage = ZW101_VERIFY_STAGE_FAILED;
                goto verify_fail;
            }
        } else if (g_ctx.last_ack == ZW101_ACK_ERR_NO_FINGER) {
            g_ctx.verify.stage = ZW101_VERIFY_STAGE_WAIT_FINGER;
        } else {
            g_ctx.verify.stage = ZW101_VERIFY_STAGE_FAILED;
            ret = ZW101_DRV_ERR_MODULE;
            goto verify_fail;
        }
        break;

    /* ---------- 生成特征中 ---------- */
    case ZW101_VERIFY_STAGE_EXTRACTING:
        if (g_ctx.last_ack == ZW101_ACK_SUCCESS) {
            /* 特征生成成功 → 搜索/比对 */
            g_ctx.verify.stage = ZW101_VERIFY_STAGE_SEARCHING;
            if (g_ctx.verify.is_identify) {
                /* 1:1 比对: 先 LoadChar 目标模板, 再 Match */
                /* 简化: 使用 search 指令指定范围 */
                ret = send_cmd_search(1, g_ctx.verify.target_id, 1);
            } else {
                /* 1:N 搜索 */
                ret = send_cmd_search(1, 0, ZW101_MAX_FINGERPRINT_COUNT);
            }
            if (ret != ZW101_DRV_OK) {
                g_ctx.verify.stage = ZW101_VERIFY_STAGE_FAILED;
                goto verify_fail;
            }
        } else {
            g_ctx.verify.stage = ZW101_VERIFY_STAGE_FAILED;
            ret = ZW101_DRV_ERR_MODULE;
            goto verify_fail;
        }
        break;

    /* ---------- 搜索中 ---------- */
    case ZW101_VERIFY_STAGE_SEARCHING:
        if (g_ctx.last_ack == ZW101_ACK_SUCCESS) {
            /* 匹配成功 */
            uint16_t page_id, score;
            zw101_parse_search_result(g_ctx.rcv_buf, g_ctx.rcv_len, &page_id, &score);

            g_ctx.verify.matched_id   = page_id;
            g_ctx.verify.match_score  = score;
            g_ctx.verify.stage        = ZW101_VERIFY_STAGE_DONE;
            g_ctx.state               = ZW101_STATE_IDLE;

            zw101_rgb_success();
            zw101_beep(150);

            if (g_ctx.verify.cb) {
                g_ctx.verify.cb(ZW101_VERIFY_STAGE_DONE, page_id, score,
                                ZW101_DRV_OK, g_ctx.verify.cb_arg);
            }
        } else if (g_ctx.last_ack == ZW101_ACK_ERR_NOT_SEARCHED) {
            /* 未找到 */
            g_ctx.verify.stage = ZW101_VERIFY_STAGE_DONE;
            g_ctx.state = ZW101_STATE_IDLE;
            zw101_rgb_failure();
            zw101_beep(100);
            if (g_ctx.verify.cb) {
                g_ctx.verify.cb(ZW101_VERIFY_STAGE_DONE, 0, 0,
                                ZW101_DRV_ERR_NOT_MATCH, g_ctx.verify.cb_arg);
            }
        } else {
            g_ctx.verify.stage = ZW101_VERIFY_STAGE_FAILED;
            ret = ZW101_DRV_ERR_MODULE;
            goto verify_fail;
        }
        break;

    /* ---------- 验证失败 ---------- */
    verify_fail:
    case ZW101_VERIFY_STAGE_FAILED:
        g_ctx.verify.stage = ZW101_VERIFY_STAGE_FAILED;
        g_ctx.state = ZW101_STATE_IDLE;
        zw101_rgb_failure();
        if (g_ctx.verify.cb) {
            g_ctx.verify.cb(ZW101_VERIFY_STAGE_FAILED, 0, 0, ret,
                            g_ctx.verify.cb_arg);
        }
        return ret;

    default:
        break;
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 取消正在进行的验证
 */
zw101_drv_err_t zw101_verify_cancel(void)
{
    if (g_ctx.state != ZW101_STATE_VERIFYING) return ZW101_DRV_ERR_BUSY;

    uint8_t cmd[32];
    uint16_t cmd_len = zw101_build_cmd_pkg(cmd, ZW101_DEFAULT_DEV_ADDR,
                                            ZW101_CMD_CANCEL, NULL, 0);
    send_cmd_and_wait_ack(cmd, cmd_len, ZW101_TIMEOUT_COMMON);

    g_ctx.verify.stage = ZW101_VERIFY_STAGE_IDLE;
    g_ctx.state = ZW101_STATE_IDLE;

    return ZW101_DRV_OK;
}

/**
 * @brief 获取验证状态
 */
zw101_drv_err_t zw101_verify_get_stage(zw101_verify_stage_t *stage_out)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;
    if (stage_out) *stage_out = g_ctx.verify.stage;
    return ZW101_DRV_OK;
}

/* ==================== 指纹删除 ==================== */

/**
 * @brief 删除指定 ID 的指纹模板
 *
 * 流程: 删除 → 清空 Buffer1 → 清空 Buffer2
 *
 * @param  page_id 指纹 ID
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_delete_finger(uint16_t page_id)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;

    g_ctx.state = ZW101_STATE_CMD_PENDING;
    zw101_drv_err_t ret = send_cmd_delete(page_id);
    g_ctx.state = ZW101_STATE_IDLE;

    return ret;
}

/**
 * @brief 清空指纹库
 *
 * 执行 PS_Empty 指令，删除全部模板。
 * 耗时较长，超时设为 2000ms。
 *
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_delete_all(void)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;

    g_ctx.state = ZW101_STATE_CMD_PENDING;
    zw101_drv_err_t ret = send_cmd_empty();
    g_ctx.state = ZW101_STATE_IDLE;

    return ret;
}

/* ==================== 指纹库查询 ==================== */

/**
 * @brief 获取已存储指纹数量
 */
zw101_drv_err_t zw101_get_stored_count(uint16_t *count_out)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;
    if (count_out == NULL)  return ZW101_DRV_ERR_PARAM;

    uint8_t cmd[32];
    uint16_t cmd_len = zw101_build_cmd_pkg(cmd, ZW101_DEFAULT_DEV_ADDR,
                                            ZW101_CMD_VALID_TEMPLATE_NUM, NULL, 0);
    zw101_drv_err_t ret = send_cmd_and_wait_ack(cmd, cmd_len, ZW101_TIMEOUT_COMMON);
    if (ret != ZW101_DRV_OK) return ret;

    *count_out = ((uint16_t)g_ctx.rcv_buf[1] << 8) | g_ctx.rcv_buf[2];
    return ZW101_DRV_OK;
}

/**
 * @brief 获取指纹索引表
 *
 * 索引表分隔为多个页, 每页 32 字节。
 * 每 1 位代表一个 ID: 1 = 已录入, 0 = 空闲。
 *
 * @param  index_table 输出缓冲区 (至少 32 字节)
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_get_index_table(uint8_t *index_table)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;
    if (index_table == NULL) return ZW101_DRV_ERR_PARAM;

    /* 读索引表 Page 0 (包含 ID 0~255) */
    uint8_t params[1] = { 0x00 }; /* 页码 0 */
    uint8_t cmd[32];
    uint16_t cmd_len = zw101_build_cmd_pkg(cmd, ZW101_DEFAULT_DEV_ADDR,
                                            ZW101_CMD_READ_INDEX_TABLE, params, 1);
    zw101_drv_err_t ret = send_cmd_and_wait_ack(cmd, cmd_len, ZW101_TIMEOUT_COMMON);
    if (ret != ZW101_DRV_OK) return ret;

    /* 应答: ack(1) + 32bytes 索引表 + checksum(2) → payload = 35 */
    memcpy(index_table, &g_ctx.rcv_buf[1], ZW101_INDEX_TABLE_SIZE);
    return ZW101_DRV_OK;
}

/**
 * @brief 查询指定 ID 是否已录入指纹
 */
zw101_drv_err_t zw101_is_id_occupied(uint16_t page_id, bool *occupied)
{
    if (page_id >= ZW101_MAX_FINGERPRINT_COUNT) return ZW101_DRV_ERR_PARAM;
    if (occupied == NULL) return ZW101_DRV_ERR_PARAM;

    uint8_t index_table[ZW101_INDEX_TABLE_SIZE];
    zw101_drv_err_t ret = zw101_get_index_table(index_table);
    if (ret != ZW101_DRV_OK) return ret;

    uint16_t byte_idx = page_id / 8u;
    uint8_t  bit_idx  = page_id % 8u;

    *occupied = (index_table[byte_idx] & (1u << bit_idx)) != 0;
    return ZW101_DRV_OK;
}

/**
 * @brief 查找一个空闲的指纹 ID
 *
 * 遍历索引表，找到第一个空闲位。
 *
 * @param  free_id_out 空闲 ID
 * @return ZW101_DRV_OK 或 ZW101_DRV_ERR_LIB_FULL
 */
zw101_drv_err_t zw101_find_free_id(uint16_t *free_id_out)
{
    uint8_t index_table[ZW101_INDEX_TABLE_SIZE];
    zw101_drv_err_t ret = zw101_get_index_table(index_table);
    if (ret != ZW101_DRV_OK) return ret;

    for (uint16_t id = 0; id < ZW101_MAX_FINGERPRINT_COUNT; id++) {
        uint16_t byte_idx = id / 8u;
        uint8_t  bit_idx  = id % 8u;
        if (!(index_table[byte_idx] & (1u << bit_idx))) {
            *free_id_out = id;
            return ZW101_DRV_OK;
        }
    }

    return ZW101_DRV_ERR_LIB_FULL;
}

/* ==================== LED / 蜂鸣器控制 ==================== */

/**
 * @brief 设置 RGB 灯效
 *
 * 指令格式 (13 字节):
 *   cmd(0x3C) + func(1) + color(1) + end_color_duty(1) + loop_times(1) + cycle(1)
 */
zw101_drv_err_t zw101_rgb_ctrl(zw101_rgb_func_t func, zw101_rgb_color_t color,
                               uint8_t end_color_duty, uint8_t loop_times, uint8_t cycle)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;

    uint8_t params[5] = { func, color, end_color_duty, loop_times, cycle };
    uint8_t cmd[32];
    uint16_t cmd_len = zw101_build_cmd_pkg(cmd, ZW101_DEFAULT_DEV_ADDR,
                                            ZW101_CMD_RGB_CTRL, params, 5);

    g_ctx.state = ZW101_STATE_CMD_PENDING;
    zw101_drv_err_t ret = send_cmd_and_wait_ack(cmd, cmd_len, ZW101_TIMEOUT_RGB);
    g_ctx.state = ZW101_STATE_IDLE;

    return ret;
}

zw101_drv_err_t zw101_rgb_standby(void)
{
    return zw101_rgb_ctrl(ZW101_RGB_BREATH, ZW101_RGB_COLOR_B,
                          ZW101_RGB_DUTY_NORMAL, 0, 0);
}

zw101_drv_err_t zw101_rgb_success(void)
{
    return zw101_rgb_ctrl(ZW101_RGB_FLICK, ZW101_RGB_COLOR_G,
                          ZW101_RGB_DUTY_SUCCESS, 2, 0);
}

zw101_drv_err_t zw101_rgb_failure(void)
{
    return zw101_rgb_ctrl(ZW101_RGB_FLICK, ZW101_RGB_COLOR_R,
                          ZW101_RGB_DUTY_SUCCESS, 2, 0);
}

zw101_drv_err_t zw101_rgb_off(void)
{
    return zw101_rgb_ctrl(ZW101_RGB_OFF, ZW101_RGB_COLOR_OFF, 0, 0, 0);
}

/**
 * @brief 蜂鸣器提示
 */
void zw101_beep(uint16_t duration_ms)
{
    if (g_ctx.hal->buzzer_ctrl) {
        g_ctx.hal->buzzer_ctrl(duration_ms, 0);
    }
}

/* ==================== 休眠与唤醒 ==================== */

/**
 * @brief 发送休眠指令
 */
zw101_drv_err_t zw101_enter_sleep(void)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;

    zw101_rgb_off();
    zw101_drv_err_t ret = send_cmd_sleep();

    /* 休眠后 TOUCH_OUT 仍有效, 可用于唤醒 */
    return ret;
}

/**
 * @brief 唤醒模组
 */
zw101_drv_err_t zw101_wakeup(void)
{
    if (!g_ctx.initialized) return ZW101_DRV_ERR_NOT_INIT;

    return zw101_power_on();
}

/* ==================== 工具函数 ==================== */

uint8_t zw101_get_last_ack(void) { return g_ctx.last_ack; }

const char *zw101_driver_version(void)
{
    static char version[16];
    static bool cached = false;
    if (!cached) {
        snprintf(version, sizeof(version), "%d.%d.%d",
                 ZW101_DRV_VERSION_MAJOR, ZW101_DRV_VERSION_MINOR,
                 ZW101_DRV_VERSION_PATCH);
        cached = true;
    }
    return version;
}

/* ==================== 内部函数: 协议操作 ==================== */

/**
 * @brief 发送命令包并等待应答
 *
 * 流程:
 *   1. 发送命令包
 *   2. 在 timeout_ms 内等待应答包 (0x07)
 *   3. 解析确认码
 *   4. 校验和验证
 *
 * @param  cmd        完整命令包
 * @param  cmd_len    命令包长度
 * @param  timeout_ms 超时时间
 * @return ZW101_DRV_OK 或错误码
 */
static zw101_drv_err_t send_cmd_and_wait_ack(const uint8_t *cmd, uint16_t cmd_len,
                                             uint32_t timeout_ms)
{
    /* 发送命令 */
    g_ctx.hal->uart_send(cmd, cmd_len);

    /* 接收应答 */
    memset(g_ctx.rcv_buf, 0, ZW101_RCV_BUF_MAX);
    g_ctx.rcv_len = g_ctx.hal->uart_recv(g_ctx.rcv_buf, ZW101_RCV_BUF_MAX, timeout_ms);
    if (g_ctx.rcv_len == 0) {
        g_ctx.last_ack = ZW101_ACK_ERR_TIME_OUT;
        return ZW101_DRV_ERR_TIMEOUT;
    }

    /* 验证最小长度和包头 */
    if (g_ctx.rcv_len < ZW101_PKG_MIN_LEN) {
        g_ctx.last_ack = ZW101_ACK_ERR_PKG_RCV;
        return ZW101_DRV_ERR_PROTO;
    }
    if (g_ctx.rcv_buf[0] != ZW101_HEADER_FIRST || g_ctx.rcv_buf[1] != ZW101_HEADER_SECOND) {
        g_ctx.last_ack = ZW101_ACK_ERR_PKG_RCV;
        return ZW101_DRV_ERR_PROTO;
    }

    /* 验证校验和 */
    uint16_t calc_sum = zw101_calc_checksum(g_ctx.rcv_buf, g_ctx.rcv_len);
    uint16_t rcv_sum  = ((uint16_t)g_ctx.rcv_buf[g_ctx.rcv_len - 2] << 8)
                       | g_ctx.rcv_buf[g_ctx.rcv_len - 1];
    if (calc_sum != rcv_sum) {
        g_ctx.last_ack = ZW101_ACK_ERR_PKG_RCV;
        return ZW101_DRV_ERR_CHECKSUM;
    }

    /* 解析确认码 (偏移 9: 固定头 8 字节 + 包标识 1 字节 = 9) */
    g_ctx.last_ack = g_ctx.rcv_buf[9];

    if (g_ctx.last_ack != ZW101_ACK_SUCCESS) {
        return ZW101_DRV_ERR_MODULE;
    }

    return ZW101_DRV_OK;
}

/**
 * @brief 发送采图指令
 *
 * @param  is_enroll true=注册采图 (0x29), false=验证采图 (0x01)
 * @return ZW101_DRV_OK 指令已发送 (应答需在 process 中处理)
 */
static zw101_drv_err_t send_cmd_get_image(bool is_enroll)
{
    uint8_t cmd = is_enroll ? ZW101_CMD_GET_ENROLL_IMAGE : ZW101_CMD_GET_IMAGE;
    uint8_t buf[32];
    uint16_t len = zw101_build_cmd_pkg(buf, ZW101_DEFAULT_DEV_ADDR, cmd, NULL, 0);
    return send_cmd_and_wait_ack(buf, len, ZW101_TIMEOUT_CAPTURE);
}

/**
 * @brief 发送生成特征指令
 *
 * @param  buffer_id 缓冲区编号 (1 或 2)
 * @return ZW101_DRV_OK
 */
static zw101_drv_err_t send_cmd_gen_char(uint8_t buffer_id)
{
    uint8_t params[1] = { buffer_id };
    uint8_t buf[32];
    uint16_t len = zw101_build_cmd_pkg(buf, ZW101_DEFAULT_DEV_ADDR,
                                        ZW101_CMD_GEN_CHAR, params, 1);
    return send_cmd_and_wait_ack(buf, len, ZW101_TIMEOUT_COMMON);
}

/**
 * @brief 发送存储模板指令
 *
 * @param  buffer_id 特征缓冲区 ID (1 或 2)
 * @param  page_id   存储位置 ID
 * @return ZW101_DRV_OK
 */
static zw101_drv_err_t send_cmd_store(uint16_t buffer_id, uint16_t page_id)
{
    uint8_t params[4] = {
        (uint8_t)(buffer_id >> 8), (uint8_t)(buffer_id),
        (uint8_t)(page_id >> 8),   (uint8_t)(page_id)
    };
    uint8_t buf[32];
    uint16_t len = zw101_build_cmd_pkg(buf, ZW101_DEFAULT_DEV_ADDR,
                                        ZW101_CMD_STORE_CHAR, params, 4);
    return send_cmd_and_wait_ack(buf, len, ZW101_TIMEOUT_COMMON);
}

/**
 * @brief 发送搜索指令 (1:N)
 *
 * @param  buffer_id 特征缓冲区 ID
 * @param  start_id  搜索起始 ID
 * @param  count     搜索数量
 * @return ZW101_DRV_OK
 */
static zw101_drv_err_t send_cmd_search(uint8_t buffer_id, uint16_t start_id, uint16_t count)
{
    uint8_t params[5] = {
        buffer_id,
        (uint8_t)(start_id >> 8), (uint8_t)(start_id),
        (uint8_t)(count >> 8),    (uint8_t)(count)
    };
    uint8_t buf[32];
    uint16_t len = zw101_build_cmd_pkg(buf, ZW101_DEFAULT_DEV_ADDR,
                                        ZW101_CMD_SEARCH, params, 5);
    return send_cmd_and_wait_ack(buf, len, ZW101_TIMEOUT_MATCH);
}

/**
 * @brief 发送 1:1 比对指令
 *
 * @param  buffer_id 特征缓冲区 ID
 * @return ZW101_DRV_OK
 */
static zw101_drv_err_t send_cmd_match(uint8_t buffer_id)
{
    uint8_t params[1] = { buffer_id };
    uint8_t buf[32];
    uint16_t len = zw101_build_cmd_pkg(buf, ZW101_DEFAULT_DEV_ADDR,
                                        ZW101_CMD_MATCH, params, 1);
    return send_cmd_and_wait_ack(buf, len, ZW101_TIMEOUT_MATCH);
}

/**
 * @brief 发送合并特征生成模板指令
 * @return ZW101_DRV_OK
 */
static zw101_drv_err_t send_cmd_reg_model(void)
{
    uint8_t buf[32];
    uint16_t len = zw101_build_cmd_pkg(buf, ZW101_DEFAULT_DEV_ADDR,
                                        ZW101_CMD_REG_MODEL, NULL, 0);
    return send_cmd_and_wait_ack(buf, len, ZW101_TIMEOUT_COMMON);
}

/**
 * @brief 发送删除模板指令 (从两个缓冲区中删除)
 *
 * 删除顺序: BufferID=1 → BufferID=2
 *
 * @param  page_id 指纹 ID
 * @return ZW101_DRV_OK
 */
static zw101_drv_err_t send_cmd_delete(uint16_t page_id)
{
    /* 删除一次即可 (指定 page_id, 两个 count=1) */
    uint8_t params[4] = {
        (uint8_t)(page_id >> 8), (uint8_t)(page_id),
        0x00, 0x01               /* count=1 */
    };
    uint8_t buf[32];
    uint16_t len = zw101_build_cmd_pkg(buf, ZW101_DEFAULT_DEV_ADDR,
                                        ZW101_CMD_DEL_CHAR, params, 4);
    return send_cmd_and_wait_ack(buf, len, ZW101_TIMEOUT_COMMON);
}

/**
 * @brief 发送清空指纹库指令
 * @return ZW101_DRV_OK
 */
static zw101_drv_err_t send_cmd_empty(void)
{
    uint8_t buf[32];
    uint16_t len = zw101_build_cmd_pkg(buf, ZW101_DEFAULT_DEV_ADDR,
                                        ZW101_CMD_EMPTY, NULL, 0);
    return send_cmd_and_wait_ack(buf, len, ZW101_TIMEOUT_EMPTY);
}

/**
 * @brief 发送休眠指令
 * @return ZW101_DRV_OK
 */
static zw101_drv_err_t send_cmd_sleep(void)
{
    uint8_t buf[32];
    uint16_t len = zw101_build_cmd_pkg(buf, ZW101_DEFAULT_DEV_ADDR,
                                        ZW101_CMD_SLEEP, NULL, 0);
    return send_cmd_and_wait_ack(buf, len, ZW101_TIMEOUT_SLEEP);
}

/**
 * @brief 发送握手指令
 * @return ZW101_DRV_OK
 */
static zw101_drv_err_t send_cmd_handshake(void)
{
    uint8_t buf[32];
    uint16_t len = zw101_build_cmd_pkg(buf, ZW101_DEFAULT_DEV_ADDR,
                                        ZW101_CMD_HANDSHAKE, NULL, 0);
    return send_cmd_and_wait_ack(buf, len, ZW101_TIMEOUT_COMMON);
}

/**
 * @brief 发送校验传感器指令
 * @return ZW101_DRV_OK
 */
static zw101_drv_err_t send_cmd_check_sensor(void)
{
    uint8_t buf[32];
    uint16_t len = zw101_build_cmd_pkg(buf, ZW101_DEFAULT_DEV_ADDR,
                                        ZW101_CMD_CHECK_SENSOR, NULL, 0);
    return send_cmd_and_wait_ack(buf, len, ZW101_TIMEOUT_COMMON);
}
