/**
 * @file    zw101_fp_driver.h
 * @brief   ZW101 指纹模块驱动层 — 高层操作封装 (录入/验证/删除/LED/休眠)
 * @note    依赖协议层 zw101_fp_protocol.h 和用户提供的 HAL 回调
 * @version 1.0
 * @date    2026-06-13
 *
 * ## 架构层次
 * ```
 * 应用层 (zw101_fp_app.c)     — 状态机、业务流程
 *    ↓
 * 驱动层 (zw101_fp_driver.c)  — 高层封装: enroll/verify/delete/rgb/sleep
 *    ↓
 * 协议层 (zw101_fp_protocol.c) — 组包/解包/校验和
 *    ↓
 * HAL 层 (用户实现)            — UART 收发、延时、GPIO 控制
 * ```
 *
 * ## 使用步骤
 * 1. 实现 zw101_hal_t 中的所有回调函数
 * 2. 调用 zw101_driver_init() 注册 HAL
 * 3. 上电后调用 zw101_self_test() 自检
 * 4. 使用 zw101_enroll_start() / zw101_verify_start() 等 API
 */

#ifndef ZW101_FP_DRIVER_H
#define ZW101_FP_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "zw101_fp_protocol.h"
#include <stdint.h>
#include <stdbool.h>

/* ==================== 超时常量 (ms) ==================== */

/** @brief 模组上电等待时间 (ms) */
#define ZW101_PWRON_WAIT_MS            300u

/** @brief 通用指令超时 (ms) */
#define ZW101_TIMEOUT_COMMON           1000u

/** @brief 采图指令超时 (ms) */
#define ZW101_TIMEOUT_CAPTURE          480u

/** @brief 休眠指令超时 (ms) */
#define ZW101_TIMEOUT_SLEEP            400u

/** @brief 清空指纹库指令超时 (ms) */
#define ZW101_TIMEOUT_EMPTY            2000u

/** @brief 搜索指纹超时 (ms) — 注意指纹数量多时需加大 */
#define ZW101_TIMEOUT_MATCH            2300u

/** @brief RGB 灯控超时 (ms) */
#define ZW101_TIMEOUT_RGB              780u

/** @brief 自检超时 (ms) */
#define ZW101_TIMEOUT_SELF_TEST        800u

/** @brief 注册时采集图像最大重试次数 */
#define ZW101_CAPTURE_MAX_RETRY        3u

/** @brief 注册时生成特征最大重试次数 */
#define ZW101_EXTRACT_MAX_RETRY        3u

/** @brief 索引表总字节数 (50 枚 / 8 = 7 字节取整至 32) */
#define ZW101_INDEX_TABLE_SIZE         32u

/* ==================== 枚举定义 ==================== */

/**
 * @brief 驱动层操作结果码
 */
typedef enum {
    ZW101_DRV_OK              = 0,  /**< 操作成功 */
    ZW101_DRV_ERR_TIMEOUT     = -1, /**< 通信超时 */
    ZW101_DRV_ERR_CHECKSUM    = -2, /**< 校验和错误 */
    ZW101_DRV_ERR_PROTO       = -3, /**< 协议帧错误 */
    ZW101_DRV_ERR_MODULE      = -4, /**< 模组返回错误 */
    ZW101_DRV_ERR_BUSY        = -5, /**< 驱动忙 */
    ZW101_DRV_ERR_PARAM       = -6, /**< 参数错误 */
    ZW101_DRV_ERR_NOT_INIT    = -7, /**< 未初始化 */
    ZW101_DRV_ERR_HW          = -8, /**< 硬件错误 */
    ZW101_DRV_ERR_CANCELED    = -9, /**< 操作被取消 */
    ZW101_DRV_ERR_NO_FINGER   = -10,/**< 无手指 */
    ZW101_DRV_ERR_LIB_FULL    = -11,/**< 指纹库满 */
    ZW101_DRV_ERR_NOT_MATCH   = -12,/**< 指纹不匹配 */
    ZW101_DRV_ERR_DUPLICATE   = -13,/**< 指纹已存在 */
} zw101_drv_err_t;

/**
 * @brief 注册阶段枚举
 */
typedef enum {
    ZW101_ENROLL_STAGE_IDLE,           /**< 空闲 */
    ZW101_ENROLL_STAGE_WAIT_FINGER,    /**< 等待手指按下 */
    ZW101_ENROLL_STAGE_CAPTURING,      /**< 采图中 */
    ZW101_ENROLL_STAGE_EXTRACTING,     /**< 生成特征中 */
    ZW101_ENROLL_STAGE_WAIT_RELEASE,   /**< 等待手指抬起 */
    ZW101_ENROLL_STAGE_MERGING,        /**< 合并模板中 */
    ZW101_ENROLL_STAGE_STORING,        /**< 存储模板中 */
    ZW101_ENROLL_STAGE_DONE,           /**< 注册完成 */
    ZW101_ENROLL_STAGE_FAILED,         /**< 注册失败 */
} zw101_enroll_stage_t;

/**
 * @brief 验证阶段枚举
 */
typedef enum {
    ZW101_VERIFY_STAGE_IDLE,           /**< 空闲 */
    ZW101_VERIFY_STAGE_WAIT_FINGER,    /**< 等待手指按下 */
    ZW101_VERIFY_STAGE_CAPTURING,      /**< 采图中 */
    ZW101_VERIFY_STAGE_EXTRACTING,     /**< 生成特征中 */
    ZW101_VERIFY_STAGE_SEARCHING,      /**< 搜索指纹库中 */
    ZW101_VERIFY_STAGE_DONE,           /**< 验证完成 */
    ZW101_VERIFY_STAGE_FAILED,         /**< 验证失败 */
} zw101_verify_stage_t;

/* ==================== HAL 层回调定义 (用户必须实现) ==================== */

/**
 * @brief ZW101 硬件抽象层接口
 *
 * 用户需根据目标平台实现这些回调：
 * - CH32x035: 使用 USARTx + GPIO
 * - STM32: 使用 HAL_UART + HAL_GPIO
 * - 其他 MCU: 依平台而定
 */
typedef struct {
    /**
     * @brief 发送数据到 ZW101 模块
     * @param data 数据指针
     * @param len  数据长度
     * @note  阻塞/非阻塞均可，驱动层通过信号量同步
     */
    void (*uart_send)(const uint8_t *data, uint16_t len);

    /**
     * @brief 从 ZW101 模块接收数据 (阻塞, 带超时)
     * @param buf       接收缓冲区
     * @param max_len   最大接收长度
     * @param timeout_ms 超时时间 (ms)
     * @return uint16_t 实际接收字节数, 0 表示超时
     */
    uint16_t (*uart_recv)(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms);

    /**
     * @brief 毫秒级延时
     * @param ms 延时毫秒数
     */
    void (*delay_ms)(uint32_t ms);

    /**
     * @brief 获取系统毫秒时间戳 (用于超时判断)
     * @return uint32_t 毫秒数
     */
    uint32_t (*get_tick_ms)(void);

    /**
     * @brief 控制指纹模组 VCC 电源 (用于低功耗管理)
     * @param enable true=上电, false=断电
     */
    void (*power_ctrl)(bool enable);

    /**
     * @brief 读取 TOUCH_OUT 引脚电平 (活体检测信号)
     * @return uint8_t 1=活体检测真 (有手指), 0=活体检测假 (无手指)
     */
    uint8_t (*read_touch_out)(void);

    /**
     * @brief 控制蜂鸣器
     * @param duration_ms 持续时间, 0=停止
     * @param freq_hz     频率, 0=默认
     */
    void (*buzzer_ctrl)(uint16_t duration_ms, uint16_t freq_hz);

    /* 可选: 临界区保护 (多任务环境) */
    void (*enter_critical)(void);
    void (*exit_critical)(void);
} zw101_hal_t;

/* ==================== 事件回调定义 ==================== */

/**
 * @brief 注册事件回调
 * @param stage     当前注册阶段
 * @param progress  进度 (0~100)
 * @param user_id   分配的指纹 ID (仅在 DONE 阶段有效)
 * @param err_code  错误码 (仅在 FAILED 阶段有效)
 * @param arg       用户自定义参数
 */
typedef void (*zw101_enroll_cb_t)(zw101_enroll_stage_t stage, uint8_t progress,
                                  uint16_t user_id, zw101_drv_err_t err_code, void *arg);

/**
 * @brief 验证事件回调
 * @param stage     当前验证阶段
 * @param user_id   匹配到的指纹 ID (仅在 DONE 阶段有效)
 * @param score     匹配得分
 * @param err_code  错误码
 * @param arg       用户自定义参数
 */
typedef void (*zw101_verify_cb_t)(zw101_verify_stage_t stage, uint16_t user_id,
                                  uint16_t score, zw101_drv_err_t err_code, void *arg);

/* ==================== 初始化与自检 ==================== */

/**
 * @brief 初始化 ZW101 驱动层
 *
 * 注册 HAL 回调, 重置内部状态。
 * 应在系统启动时调用。
 *
 * @param  hal HAL 接口指针 (必须指向静态/全局变量)
 * @return ZW101_DRV_OK 成功
 */
zw101_drv_err_t zw101_driver_init(const zw101_hal_t *hal);

/**
 * @brief 模组上电并等待初始化完成
 *
 * 控制 VCC 上电，等待模组内部初始化。
 * 模组初始化成功后会上传握手信号 0x55。
 *
 * @return ZW101_DRV_OK 成功
 *         ZW101_DRV_ERR_TIMEOUT 上电超时
 */
zw101_drv_err_t zw101_power_on(void);

/**
 * @brief 模组断电
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_power_off(void);

/**
 * @brief 模组自检流程
 *
 * 执行: 握手 → 读系统参数 → 校验传感器 → 读有效模板数
 *
 * @param  para_out        输出系统参数 (可为 NULL)
 * @param  add_para_out    输出附加参数 (可为 NULL)
 * @param  stored_count_out 已存储指纹数量 (可为 NULL)
 * @return ZW101_DRV_OK 自检通过
 */
zw101_drv_err_t zw101_self_test(zw101_sys_para_t *para_out,
                                zw101_add_para_t *add_para_out,
                                uint16_t *stored_count_out);

/* ==================== 指纹录入 ==================== */

/**
 * @brief 开始指纹录入流程
 *
 * 主控应周期性调用 zw101_enroll_process() 推进状态机。
 * 注册完成时通过回调通知。
 *
 * @note 本函数为异步接口, 返回后需在主循环中轮询 process()
 *
 * @param  enroll_count 录入次数, 0=使用模组默认值
 * @param  cb           阶段回调 (可为 NULL)
 * @param  arg          用户自定义参数
 * @return ZW101_DRV_OK         启动成功
 *         ZW101_DRV_ERR_BUSY   驱动忙 (上一次操作未完成)
 *         ZW101_DRV_ERR_LIB_FULL 指纹库已满
 */
zw101_drv_err_t zw101_enroll_start(uint8_t enroll_count,
                                   zw101_enroll_cb_t cb, void *arg);

/**
 * @brief 推进注册状态机 (需在主循环中周期性调用)
 *
 * @return ZW101_DRV_OK         状态推进中
 *         ZW101_DRV_ERR_xxx    错误
 */
zw101_drv_err_t zw101_enroll_process(void);

/**
 * @brief 取消正在进行的注册
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_enroll_cancel(void);

/**
 * @brief 获取当前注册进度
 * @param  stage_out    当前阶段
 * @param  progress_out 进度 0~100
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_enroll_get_progress(zw101_enroll_stage_t *stage_out,
                                          uint8_t *progress_out);

/* ==================== 指纹验证 / 搜索 ==================== */

/**
 * @brief 开始 1:N 指纹验证 (搜索模式)
 *
 * 采集指纹后在整个指纹库中搜索。
 *
 * @param  cb   阶段回调
 * @param  arg  用户自定义参数
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_verify_start(zw101_verify_cb_t cb, void *arg);

/**
 * @brief 开始 1:1 指纹比对 (指定 ID 比对)
 *
 * 采集指纹后与指定 ID 的模板比对。
 *
 * @param  target_id 目标指纹 ID
 * @param  cb        阶段回调
 * @param  arg       用户自定义参数
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_identify_start(uint16_t target_id,
                                     zw101_verify_cb_t cb, void *arg);

/**
 * @brief 推进验证状态机 (需在主循环中周期性调用)
 * @return ZW101_DRV_OK 或错误码
 */
zw101_drv_err_t zw101_verify_process(void);

/**
 * @brief 取消正在进行的验证
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_verify_cancel(void);

/**
 * @brief 获取验证状态
 */
zw101_drv_err_t zw101_verify_get_stage(zw101_verify_stage_t *stage_out);

/* ==================== 指纹删除 ==================== */

/**
 * @brief 删除指定 ID 的指纹模板
 *
 * @param  page_id 指纹 ID (0 ~ ZW101_MAX_FINGERPRINT_COUNT-1)
 * @return ZW101_DRV_OK 删除成功
 */
zw101_drv_err_t zw101_delete_finger(uint16_t page_id);

/**
 * @brief 清空指纹库 (删除全部模板)
 * @return ZW101_DRV_OK 清空成功
 */
zw101_drv_err_t zw101_delete_all(void);

/* ==================== 指纹库查询 ==================== */

/**
 * @brief 获取已存储指纹数量
 *
 * @param  count_out 有效模板数量
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_get_stored_count(uint16_t *count_out);

/**
 * @brief 获取指纹索引表 (每位代表一个 ID 是否已录入)
 *
 * @param  index_table 输出缓冲区 (至少 32 字节)
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_get_index_table(uint8_t *index_table);

/**
 * @brief 查询指定 ID 是否已录入指纹
 *
 * @param  page_id   指纹 ID
 * @param  occupied   true=已录入, false=空闲
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_is_id_occupied(uint16_t page_id, bool *occupied);

/**
 * @brief 查找一个空闲的指纹 ID
 *
 * @param  free_id_out 空闲 ID
 * @return ZW101_DRV_OK 找到空闲 ID
 *         ZW101_DRV_ERR_LIB_FULL 指纹库已满
 */
zw101_drv_err_t zw101_find_free_id(uint16_t *free_id_out);

/* ==================== LED / 蜂鸣器控制 ==================== */

/**
 * @brief 设置 RGB 灯效
 *
 * @param  func       灯效功能 (呼吸/闪烁/常亮/关)
 * @param  color      颜色
 * @param  end_color_duty 结束颜色占空比
 * @param  loop_times 循环次数 (0=无限)
 * @param  cycle      周期
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_rgb_ctrl(zw101_rgb_func_t func, zw101_rgb_color_t color,
                               uint8_t end_color_duty, uint8_t loop_times, uint8_t cycle);

/**
 * @brief RGB 快捷: 蓝色呼吸灯 (待机指示)
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_rgb_standby(void);

/**
 * @brief RGB 快捷: 绿色闪烁 (成功提示)
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_rgb_success(void);

/**
 * @brief RGB 快捷: 红色闪烁 (失败提示)
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_rgb_failure(void);

/**
 * @brief RGB 快捷: 关闭
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_rgb_off(void);

/**
 * @brief 蜂鸣器提示
 * @param  duration_ms 持续时间
 */
void zw101_beep(uint16_t duration_ms);

/* ==================== 休眠与唤醒 ==================== */

/**
 * @brief 发送休眠指令, 使模组进入低功耗模式
 *
 * @note  休眠后, TOUCH_OUT 引脚可唤醒 MCU, 唤醒后需重新走自检/握手机制
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_enter_sleep(void);

/**
 * @brief 唤醒模组 (重新上电 + 自检)
 * @return ZW101_DRV_OK
 */
zw101_drv_err_t zw101_wakeup(void);

/* ==================== 工具函数 ==================== */

/**
 * @brief 获取最后一次操作的模组原始错误码
 * @return uint8_t 确认码
 */
uint8_t zw101_get_last_ack(void);

/**
 * @brief 获取驱动版本字符串
 * @return const char* 版本号
 */
const char *zw101_driver_version(void);

#ifdef __cplusplus
}
#endif

#endif /* ZW101_FP_DRIVER_H */
