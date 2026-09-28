/**
* @file ws2812_spi.h
* @brief WS2812 SPI 后端对外接口声明（3 SPI 位编码 1 WS2812 数据位）
*
* 所属模块：WS2812 LED 驱动（SPI 后端层，连接管理层与平台 PHY）。
* 对应实现：ws2812_spi.c；平台 PHY：ws2812_phy_ch582m.h / ws2812_phy_ch582m.c。
*
* 分层职责：
*   - 本层：颜色顺序映射 + 3bit→1bit 编码 + DMA 缓冲组装 + 后端调度；
*   - PHY 层（ws2812_phy_*）：SPI 初始化 + DMA 启动 + 完成回调。
*
* 编码原理（每 WS2812 数据位 → 3 SPI 位）：
*   WS2812 逻辑 0 → SPI 码 100 (0x04) → 高电平 1/3 周期
*   WS2812 逻辑 1 → SPI 码 110 (0x06) → 高电平 2/3 周期
*   SPI 时钟 2.4MHz → 每 SPI 位 ~417ns → 3 位 ~1250ns，匹配 WS2812 时序
*
* 重置序列：SPI 连续发送 0（低电平）超过 WS2812_RESET_US 后锁存。
*   reset_bytes = ceil(spi_hz × reset_us / 10^6 / 8)。
*
* 使用约定：
*   1. ws2812_spi_configure() → 绑定 tx_buffer + 时序参数 → 得到 backend；
*   2. 把 backend_context = &backend 填入 ws2812_config_t → ws2812_init()；
*   3. ws2812_init 内部自动调 ws2812_spi_backend_init → ws2812_spi_phy_init。
*/
#ifndef WS2812_SPI_H
#define WS2812_SPI_H

#include "ws2812.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 时序参数 ===== */
#define WS2812_SPI_DEFAULT_HZ 2400000UL   /**< 默认 SPI 时钟频率（Hz）—— 2.4MHz，匹配 WS2812 时序 */
#define WS2812_DEFAULT_RESET_US 300UL     /**< 默认 RESET 低电平保持时间（微秒）—— >280us 锁存 */
#define WS2812_SPI_BYTES_PER_PIXEL 9UL    /**< 每灯珠编码后字节数 = 3 通道 × 3 SPI 字节/通道 */

/**
 * @brief  计算 RESET 序列所需 SPI 发送字节数
 *
 * 公式：reset_bytes = ceil(spi_hz × reset_us / 8,000,000)
 *       即 SPI 发送 reset_bytes 个字节的时间 = reset_us
 */
#define WS2812_SPI_RESET_BYTES(spi_hz, reset_us)                                                                       \
    ((size_t)((((uint64_t)(spi_hz) * (uint64_t)(reset_us)) + 7999999ULL) / 8000000ULL))

/**
 * @brief  计算完整 SPI 帧缓冲大小（灯珠编码 + RESET）
 *
 * 编译时可用于静态分配 DMA 缓冲：
 *   uint8_t tx_buf[WS2812_SPI_BUFFER_SIZE(LED_COUNT, WS2812_SPI_DEFAULT_HZ, WS2812_DEFAULT_RESET_US)];
 */
#define WS2812_SPI_BUFFER_SIZE(pixel_count, spi_hz, reset_us)                                                          \
    ((size_t)(pixel_count) * WS2812_SPI_BYTES_PER_PIXEL + WS2812_SPI_RESET_BYTES((spi_hz), (reset_us)))

/** @brief SPI 后端初始化配置（由应用层构造，传入 ws2812_spi_configure） */
typedef struct
{
    void *phy_user;             /**< 平台 PHY 上下文（ws2812_phy_ch582m_t*） */
    uint8_t *tx_buffer;         /**< DMA 发送缓冲（应用层分配，必须 4 字节对齐） */
    size_t tx_buffer_size;      /**< 缓冲总大小（字节） */
    uint32_t spi_hz;            /**< SPI 时钟频率（Hz，通常 2.4MHz） */
    uint32_t reset_us;          /**< RESET 低电平保持时间（微秒，通常 300us） */
} ws2812_spi_config_t;

/** @brief SPI 后端运行上下文（由 ws2812_spi_configure 填充） */
typedef struct
{
    ws2812_spi_config_t config;   /**< 初始化配置快照 */
    size_t frame_size;             /**< 编码后帧大小（字节，= pixels×9 + reset_bytes） */
    uint8_t initialized;           /**< 初始化完成标志（1=backend_init 已调过） */
} ws2812_spi_t;

/* ===== 平台 PHY 符号（弱引用，必须在平台层实现） ===== */
/**
 * @brief  SPI 物理层初始化（平台层实现，弱符号默认返回 -1）
 * @param user  PHY 上下文（ws2812_phy_ch582m_t*）
 * @param target_hz  目标 SPI 时钟频率（Hz）
 * @return 0=成功；-1=失败
 */
int ws2812_spi_phy_init(void *user, uint32_t target_hz);

/**
 * @brief  SPI DMA 异步发送（平台层实现，弱符号默认返回 -1）
 *
 * 发送完成后必须调用 ws2812_transfer_complete(owner) 通知管理层。
 *
 * @param user  PHY 上下文
 * @param data  DMA 源地址（必须 4 字节对齐）
 * @param length  发送长度（字节）
 * @param owner  WS2812 管理层句柄（传给 transfer_complete）
 * @return 0=成功启动；-1=失败
 */
int ws2812_spi_phy_transmit(void *user, const uint8_t *data, size_t length, ws2812_t *owner);

/* ===== SPI 后端对外接口 ===== */

/**
 * @brief  计算给定参数下 SPI 帧所需的 DMA 缓冲大小
 * @param pixel_count  灯珠数量
 * @param spi_hz  SPI 时钟频率（Hz）
 * @param reset_us  RESET 时间（微秒）
 * @return 所需字节数；0=参数非法（pixel_count=0 / spi_hz=0 / reset_us=0 / 溢出）
 */
size_t ws2812_spi_required_buffer_size(uint16_t pixel_count, uint32_t spi_hz, uint32_t reset_us);

/**
 * @brief  配置 SPI 后端（拷贝 config + 计算 frame_size）
 *
 * 调用后 backend 可传入 ws2812_init() 或 ws2812_spi_backend_init()。
 * 本函数不启动 PHY，仅做参数绑定与缓冲大小校验。
 *
 * @param backend  输出后端上下文（由调用者分配）
 * @param config  初始化配置（tx_buffer 必须非 NULL）
 * @param pixel_count  灯珠数量（用于计算 frame_size）
 * @return WS2812_OK / RET_NULL / RET_PARAM / RET_BUFFER
 */
ws2812_ret_t ws2812_spi_configure(ws2812_spi_t *backend, const ws2812_spi_config_t *config, uint16_t pixel_count);

/**
 * @brief  SPI 后端初始化（调用平台 PHY init）
 *
 * ws2812_init() 内部自动调用，不建议应用层直接调。
 *
 * @param backend  已 configure 的后端上下文
 * @return WS2812_OK / RET_NULL / RET_PARAM / RET_IO（PHY init 失败）
 */
ws2812_ret_t ws2812_spi_backend_init(ws2812_spi_t *backend);

/**
 * @brief  编码像素到 DMA 缓冲并启动 SPI DMA 发送（异步）
 *
 * 流程：ordered_channels → encode_channel × 3 × N → memset 填 RESET → phy_transmit。
 * DMA 完成后 PHY 层必须调 ws2812_transfer_complete(owner)。
 *
 * @param backend  已初始化的 SPI 后端
 * @param pixels  像素数组（由管理层传入）
 * @param pixel_count  像素数量
 * @param order  颜色字节序（GRB/RGB/...）
 * @param owner  WS2812 管理层句柄（DMA 完成回调时传入）
 * @return WS2812_OK / RET_NULL / RET_PARAM / RET_BUFFER / RET_IO
 */
ws2812_ret_t ws2812_spi_backend_start(ws2812_spi_t *backend, const ws2812_rgb_t *pixels, uint16_t pixel_count,
                                          ws2812_color_order_t order, ws2812_t *owner);

#ifdef __cplusplus
}
#endif

#endif /* WS2812_SPI_H */
