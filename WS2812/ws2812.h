/**
* @file ws2812.h
* @brief 平台无关的 WS2812 像素管理对外接口声明
*
* 所属模块：WS2812 LED 驱动（像素管理层，不直接操作硬件）。
* 底层后端：SPI 后端（ws2812_spi.h / ws2812_spi.c）或 PWM 后端（ws2812_pwm.h / ws2812_pwm.c），
*           由 ws2812_config_t.backend 字段选择。
*
* 分层职责：
*   - 本层（ws2812.{h,c}）：像素缓冲管理、颜色顺序映射、后端调度、忙状态机；
*   - 后端层：时序生成（SPI 发送 0 码/1 码、PWM 占空比 + DMA）。
*
* 使用约定：
*   1. 构造 ws2812_config_t（像素数组 + 数量 + 颜色顺序 + 后端 + 后端上下文）；
*   2. 调用 ws2812_init() 初始化（内部自动调用后端 init 并清空像素缓冲）；
*   3. 修改像素（set / fill / clear）→ ws2812_show() 触发传输；
*   4. 传输完成中断中调用 ws2812_transfer_complete()；
*   5. 主循环轮询 ws2812_is_busy() 或 ws2812_take_complete() 同步。
*/
#ifndef WS2812_H
#define WS2812_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief 返回码（负值表示错误） */
typedef enum
{
    WS2812_OK = 0,                  /**< 成功 */
    WS2812_RET_NULL = -1,           /**< 空指针参数 */
    WS2812_RET_PARAM = -2,          /**< 参数非法（未初始化 / backend 无效 / backend_context 为 NULL） */
    WS2812_RET_RANGE = -3,          /**< 像素索引越界 */
    WS2812_RET_BUFFER = -4,         /**< 后端缓冲不足 */
    WS2812_RET_BUSY = -5,           /**< 传输忙中 */
    WS2812_RET_IO = -6,             /**< 后端 IO 错误 */
} ws2812_ret_t;

/** @brief 颜色字节序（WS2812 默认 GRB，部分兼容灯珠使用 RGB/BGR 等） */
typedef enum
{
    WS2812_ORDER_GRB = 0,   /**< GRB（WS2812 默认） */
    WS2812_ORDER_RGB,       /**< RGB */
    WS2812_ORDER_RBG,       /**< RBG */
    WS2812_ORDER_GBR,       /**< GBR */
    WS2812_ORDER_BRG,       /**< BRG */
    WS2812_ORDER_BGR,       /**< BGR */
} ws2812_color_order_t;

/** @brief 单像素 RGB 值（应用层写入时的逻辑颜色，实际线上顺序由 color_order 决定） */
typedef struct
{
    uint8_t r;   /**< 红（0..255） */
    uint8_t g;   /**< 绿（0..255） */
    uint8_t b;   /**< 蓝（0..255） */
} ws2812_rgb_t;

/** @brief 后端类型选择（决定 ws2812_show() 调用哪个后端 start 函数） */
typedef enum
{
    WS2812_BACKEND_SPI = 0,   /**< SPI 后端：800kHz SPI + 0/1 码映射 */
    WS2812_BACKEND_PWM,       /**< PWM 后端：PWM 占空比 + DMA 级联 */
} ws2812_backend_t;

/** @brief 初始化配置（由应用层构造，传入 ws2812_init） */
typedef struct
{
    ws2812_rgb_t *pixels;           /**< 像素缓冲数组（应用层分配，本层不 malloc） */
    uint16_t pixel_count;           /**< 像素数量（≤数组长度） */
    ws2812_color_order_t color_order; /**< 颜色字节序 */
    ws2812_backend_t backend;       /**< 后端类型（WS2812_BACKEND_SPI / PWM） */
    void *backend_context;          /**< 后端上下文指针（ws2812_spi_t* 或 ws2812_pwm_t*） */
} ws2812_config_t;

/** @brief WS2812 设备运行句柄（由 ws2812_init 填充） */
typedef struct ws2812_t
{
    ws2812_rgb_t *pixels;           /**< 像素缓冲数组（同 config->pixels） */
    uint16_t pixel_count;           /**< 像素数量 */
    ws2812_color_order_t color_order; /**< 颜色字节序 */
    ws2812_backend_t backend;       /**< 后端类型 */
    void *backend_context;          /**< 后端上下文指针 */
    volatile uint8_t busy;          /**< 传输忙标志（1=后端传输中） */
    volatile uint8_t complete_pending; /**< 完成挂起标志（transfer_complete 置位，take_complete 清零） */
    uint8_t initialized;            /**< 初始化完成标志（1=已调 ws2812_init 成功） */
} ws2812_t;

/* ===== 生命周期 ===== */

/**
 * @brief  初始化 WS2812 设备句柄
 *
 * 调用后端 backend_init、清空像素缓冲、置 initialized=1。
 * 失败时自动 memset(dev, 0) 回滚，保证 dev 处于可重新 init 的干净状态。
 *
 * @param dev  输出设备句柄（由调用者分配）
 * @param config  初始化配置（全部字段必须填充，pixels/backend_context 不能为 NULL）
 * @return WS2812_OK 成功；WS2812_RET_NULL / PARAM / 后端返回码
 */
ws2812_ret_t ws2812_init(ws2812_t *dev, const ws2812_config_t *config);

/* ===== 像素修改 ===== */

/**
 * @brief  设置单个像素的 RGB 值（不触发传输）
 *
 * @param dev  已初始化设备句柄
 * @param index  像素索引（0 基）
 * @param r  红（0..255）
 * @param g  绿（0..255）
 * @param b  蓝（0..255）
 * @return WS2812_OK 成功；WS2812_RET_RANGE / PARAM / NULL
 */
ws2812_ret_t ws2812_set_pixel_rgb(ws2812_t *dev, uint16_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief  设置单个像素（24 位十六进制颜色，0xRRGGBB）
 * @param dev  已初始化设备句柄
 * @param index  像素索引
 * @param rgb  24 位颜色（bit23..16=R, bit15..8=G, bit7..0=B）
 * @return 同 ws2812_set_pixel_rgb
 */
ws2812_ret_t ws2812_set_pixel_hex(ws2812_t *dev, uint16_t index, uint32_t rgb);

/**
 * @brief  将全部像素填充为指定 RGB 值（不触发传输）
 */
ws2812_ret_t ws2812_fill_rgb(ws2812_t *dev, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief  将全部像素填充为指定十六进制颜色（不触发传输）
 */
ws2812_ret_t ws2812_fill_hex(ws2812_t *dev, uint32_t rgb);

/**
 * @brief  清空全部像素（填充黑色，不触发传输）
 */
ws2812_ret_t ws2812_clear(ws2812_t *dev);

/* ===== 传输控制 ===== */

/**
 * @brief  触发一次后端传输（异步，立即返回）
 *
 * 设置 busy=1，调用后端 backend_start 启动 DMA/SPI 发送。
 * 传输完成时，后端中断应调用 ws2812_transfer_complete() 清除 busy。
 *
 * @param dev  已初始化设备句柄
 * @return WS2812_OK 成功；WS2812_RET_BUSY 上一帧未传完；WS2812_RET_PARAM / NULL / 后端返回码
 */
ws2812_ret_t ws2812_show(ws2812_t *dev);

/**
 * @brief  传输完成回调（由后端 DMA/SPI 完成中断调用）
 *
 * 清除 busy 标志，置位 complete_pending。ISR 中安全，内部无阻塞操作。
 *
 * @param dev  设备句柄
 */
void ws2812_transfer_complete(ws2812_t *dev);

/**
 * @brief  查询传输是否忙
 * @return 1=后端传输中；0=空闲（可调用 show）
 */
uint8_t ws2812_is_busy(const ws2812_t *dev);

/**
 * @brief  原子性取出并清除 complete_pending 标志
 *
 * @return 1=自上次取出以来有传输完成；0=未完成
 */
uint8_t ws2812_take_complete(ws2812_t *dev);

/* ===== 便捷接口：修改 + show 一步完成 ===== */
/* 逻辑：先调 set/fill/clear，若 OK 则调 show。show 返回 BUSY 则整体返回 BUSY。 */
ws2812_ret_t ws2812_set_pixel_rgb_and_show(ws2812_t *dev, uint16_t index, uint8_t r, uint8_t g, uint8_t b);
ws2812_ret_t ws2812_set_pixel_hex_and_show(ws2812_t *dev, uint16_t index, uint32_t rgb);
ws2812_ret_t ws2812_fill_rgb_and_show(ws2812_t *dev, uint8_t r, uint8_t g, uint8_t b);
ws2812_ret_t ws2812_fill_hex_and_show(ws2812_t *dev, uint32_t rgb);
ws2812_ret_t ws2812_clear_and_show(ws2812_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* WS2812_H */
