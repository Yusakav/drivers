/**
* @file ws2812_spi.c
* @brief WS2812 SPI 后端实现（3 SPI 位编码 1 WS2812 数据位）
*
* 所属模块：WS2812 LED 驱动（SPI 后端层）。
* 对应头文件：ws2812_spi.h；平台 PHY：ws2812_phy_ch582m.h / .c（弱符号覆盖）。
*
* 本文件实现：
*   - 颜色字节序映射（ordered_channels：6 种 GRB/RGB/RBG/GBR/BRG/BGR）；
*   - WS2812 编码（encode_channel：每数据位 → 3 SPI 位，1→110/0→100）；
*   - DMA 缓冲组装（ws2812_spi_backend_start：逐灯珠编码 + 尾部 RESET 序列）；
*   - 缓冲大小计算（ws2812_spi_required_buffer_size：溢出安全的 uint64 运算）；
*   - 平台 PHY 弱符号（ws2812_spi_phy_init / ws2812_spi_phy_transmit：默认返回 -1）。
*
* 编码时序：
*   SPI CLK 2.4MHz → T=416.7ns → 3bit=1250ns
*   WS2812 T0H~250ns, T1H~600ns, 0/1 判别阈值 ~500ns
*   SPI 码 100 → 高 417ns → ≈ T0H
*   SPI 码 110 → 高 833ns → ≈ T1H
*   完整帧末尾发 0（SPI 空闲=低）→ RESET 序列
*/
#include "ws2812_spi.h"

#include <stdint.h>
#include <string.h>

/* ===== 弱符号定义（GCC/Clang 支持 __attribute__((weak))，其他编译器空定义） ===== */
#if defined(__GNUC__) || defined(__clang__)
#define WS2812_WEAK __attribute__((weak))
#else
#define WS2812_WEAK
#endif

/**
 * @brief  平台 PHY 初始化弱符号（默认返回 -1）
 *
 * 平台层（如 ws2812_phy_ch582m.c）提供强符号覆盖此默认实现。
 * 未链接平台实现时调用返回 -1，导致 ws2812_spi_backend_init 返回 RET_IO。
 */
WS2812_WEAK int ws2812_spi_phy_init(void *user, uint32_t target_hz)
{
    (void)user;
    (void)target_hz;
    return -1;
}

/**
 * @brief  平台 PHY 发送弱符号（默认返回 -1）
 *
 * 平台层提供强符号覆盖。未链接时调用返回 -1，导致 backend_start 返回 RET_IO。
 */
WS2812_WEAK int ws2812_spi_phy_transmit(void *user, const uint8_t *data, size_t length, ws2812_t *owner)
{
    (void)user;
    (void)data;
    (void)length;
    (void)owner;
    return -1;
}

/* ===== 内部辅助函数 ===== */

/**
 * @brief  按指定颜色顺序提取像素的 3 通道值
 *
 * 应用层始终以 R/G/B 逻辑顺序写入 pixels，实际线上顺序由本函数按 order 重排。
 * order=GRB（WS2812 默认）时输出 [G, R, B]。
 *
 * @param pixel  输入像素（逻辑 R/G/B）
 * @param order  颜色字节序
 * @param channels  输出 3 通道数组（[0]=MSB 通道, [2]=LSB 通道）
 */
static void ordered_channels(const ws2812_rgb_t *pixel, ws2812_color_order_t order, uint8_t channels[3])
{
    switch (order)
    {
    case WS2812_ORDER_RGB:
        channels[0] = pixel->r;
        channels[1] = pixel->g;
        channels[2] = pixel->b;
        break;
    case WS2812_ORDER_RBG:
        channels[0] = pixel->r;
        channels[1] = pixel->b;
        channels[2] = pixel->g;
        break;
    case WS2812_ORDER_GBR:
        channels[0] = pixel->g;
        channels[1] = pixel->b;
        channels[2] = pixel->r;
        break;
    case WS2812_ORDER_BRG:
        channels[0] = pixel->b;
        channels[1] = pixel->r;
        channels[2] = pixel->g;
        break;
    case WS2812_ORDER_BGR:
        channels[0] = pixel->b;
        channels[1] = pixel->g;
        channels[2] = pixel->r;
        break;
    case WS2812_ORDER_GRB:
    default:
        channels[0] = pixel->g;
        channels[1] = pixel->r;
        channels[2] = pixel->b;
        break;
    }
}

/**
 * @brief  将 1 个数据字节编码为 3 字节 SPI 序列（24bit = 8×3bit）
 *
 * 编码规则：MSB first，每数据位 → 3 SPI 位
 *   data bit 0 → SPI 码 100 = 0x04（高电平 1/3 周期 ≈ WS2812 T0H）
 *   data bit 1 → SPI 码 110 = 0x06（高电平 2/3 周期 ≈ WS2812 T1H）
 *
 * 内部用 uint32_t encoded 左移累积 24bit，最后拆成 3 字节写入 output[0..2]。
 *
 * @param output  输出 3 字节缓冲
 * @param value  输入数据（0..255）
 */
static void encode_channel(uint8_t *output, uint8_t value)
{
    uint32_t encoded = 0U;
    uint8_t bit;

    for (bit = 0U; bit < 8U; ++bit)
    {
        encoded <<= 3U;
        encoded |= ((value & (uint8_t)(0x80U >> bit)) != 0U) ? 0x06U : 0x04U;
    }
    output[0] = (uint8_t)(encoded >> 16U);
    output[1] = (uint8_t)(encoded >> 8U);
    output[2] = (uint8_t)encoded;
}

/* ===== SPI 后端对外接口 ===== */

/**
 * @brief  计算给定参数下 SPI 帧所需的 DMA 缓冲大小
 *
 * 溢出安全：使用 uint64 运算，检查 total ≤ SIZE_MAX；参数为 0 时返回 0。
 * reset_bytes=0 （reset_us×spi_hz 乘积太小）时也返回 0（无法保证 RESET 时间）。
 *
 * @param pixel_count  灯珠数量
 * @param spi_hz  SPI 时钟频率（Hz）
 * @param reset_us  RESET 时间（微秒）
 * @return 所需字节数；0=参数非法或溢出
 */
size_t ws2812_spi_required_buffer_size(uint16_t pixel_count, uint32_t spi_hz, uint32_t reset_us)
{
    uint64_t reset_bytes;
    uint64_t total;

    if ((pixel_count == 0U) || (spi_hz == 0U) || (reset_us == 0U))
    {
        return 0U;
    }
    reset_bytes = (((uint64_t)spi_hz * reset_us) + 7999999ULL) / 8000000ULL;
    total = ((uint64_t)pixel_count * WS2812_SPI_BYTES_PER_PIXEL) + reset_bytes;
    if ((reset_bytes == 0U) || (total > (uint64_t)SIZE_MAX))
    {
        return 0U;
    }
    return (size_t)total;
}

/**
 * @brief  配置 SPI 后端（拷贝 config + 计算 frame_size）
 *
 * 校验 tx_buffer 非 NULL、缓冲大小 ≥ required。memset 清空 backend 后填充字段。
 *
 * @param backend  输出后端上下文
 * @param config  初始化配置
 * @param pixel_count  灯珠数量
 * @return WS2812_OK / RET_NULL / RET_PARAM / RET_BUFFER
 */
ws2812_ret_t ws2812_spi_configure(ws2812_spi_t *backend, const ws2812_spi_config_t *config, uint16_t pixel_count)
{
    size_t required;

    if ((backend == NULL) || (config == NULL))
    {
        return WS2812_RET_NULL;
    }
    if (config->tx_buffer == NULL)
    {
        return WS2812_RET_PARAM;
    }
    required = ws2812_spi_required_buffer_size(pixel_count, config->spi_hz, config->reset_us);
    if (required == 0U)
    {
        return WS2812_RET_PARAM;
    }
    if (config->tx_buffer_size < required)
    {
        return WS2812_RET_BUFFER;
    }

    memset(backend, 0, sizeof(*backend));
    backend->config = *config;
    backend->frame_size = required;
    return WS2812_OK;
}

/**
 * @brief  SPI 后端初始化（调用平台 PHY init）
 *
 * ws2812_init() 内部自动调用，不建议应用层直接调。
 * 平台 PHY init 失败时返回 RET_IO（弱符号默认返回 -1 也会导致此错误）。
 *
 * @param backend  已 configure 的后端
 * @return WS2812_OK / RET_NULL / RET_PARAM / RET_IO
 */
ws2812_ret_t ws2812_spi_backend_init(ws2812_spi_t *backend)
{
    if (backend == NULL)
    {
        return WS2812_RET_NULL;
    }
    if (backend->frame_size == 0U)
    {
        return WS2812_RET_PARAM;
    }
    if (ws2812_spi_phy_init(backend->config.phy_user, backend->config.spi_hz) != 0)
    {
        return WS2812_RET_IO;
    }
    backend->initialized = 1U;
    return WS2812_OK;
}

/**
 * @brief  编码像素到 DMA 缓冲并启动 SPI DMA 发送（异步）
 *
 * 编码流程：
 *   for 每个灯珠 → ordered_channels → for 每通道 → encode_channel → offset 递增 3
 *   memset(tx_buffer + offset, 0, reset_bytes) → 填 RESET 序列（SPI 空闲=低）
 *   phy_transmit(tx_buffer, frame_size, owner) → 启动 DMA
 *
 * @param backend  已初始化的 SPI 后端
 * @param pixels  像素数组
 * @param pixel_count  像素数量
 * @param order  颜色字节序
 * @param owner  WS2812 管理层句柄（DMA 完成回调时传入）
 * @return WS2812_OK / RET_NULL / RET_PARAM / RET_BUFFER / RET_IO
 */
ws2812_ret_t ws2812_spi_backend_start(ws2812_spi_t *backend, const ws2812_rgb_t *pixels, uint16_t pixel_count,
                                      ws2812_color_order_t order, ws2812_t *owner)
{
    size_t required;
    size_t offset = 0U;
    uint16_t pixel_index;
    uint8_t channel_index;

    if ((backend == NULL) || (pixels == NULL) || (owner == NULL))
    {
        return WS2812_RET_NULL;
    }
    if (backend->initialized == 0U)
    {
        return WS2812_RET_PARAM;
    }
    required = ws2812_spi_required_buffer_size(pixel_count, backend->config.spi_hz, backend->config.reset_us);
    if ((required == 0U) || (required > backend->config.tx_buffer_size))
    {
        return WS2812_RET_BUFFER;
    }

    for (pixel_index = 0U; pixel_index < pixel_count; ++pixel_index)
    {
        uint8_t channels[3];
        ordered_channels(&pixels[pixel_index], order, channels);
        for (channel_index = 0U; channel_index < 3U; ++channel_index)
        {
            encode_channel(&backend->config.tx_buffer[offset], channels[channel_index]);
            offset += 3U;
        }
    }
    memset(&backend->config.tx_buffer[offset], 0, required - offset);

    if (ws2812_spi_phy_transmit(backend->config.phy_user, backend->config.tx_buffer, required, owner) != 0)
    {
        return WS2812_RET_IO;
    }
    return WS2812_OK;
}

#undef WS2812_WEAK
