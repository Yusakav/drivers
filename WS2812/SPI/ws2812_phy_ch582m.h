/**
* @file ws2812_phy_ch582m.h
* @brief CH582M SPI0 PHY 层对外接口声明（覆盖 ws2812_spi.h 中的弱符号）
*
* 所属模块：WS2812 LED 驱动（平台 PHY 层，绑定 CH582M SPI0 + DMA）。
* 对应实现：ws2812_phy_ch582m.c；弱符号声明：ws2812_spi.h。
*
* PHY 层职责：
*   - SPI0 初始化（引脚重映射 + 主模式 + 时钟分频 + Mode3 + 高位在前）；
*   - DMA 异步发送（SPI0_MasterDMATrans）；
*   - 要求 DMA 源地址 4 字节对齐（CH582M SPI DMA 硬件约束）。
*
* 弱符号覆盖机制：ws2812_spi.c 中 ws2812_spi_phy_init / ws2812_spi_phy_transmit
* 声明为 __attribute__((weak))，本文件的强符号实现自动覆盖默认返回 -1 的桩实现。
* 应用层需同时链接 ws2812_spi.c 和 ws2812_phy_ch582m.c 才能正常工作。
*/
#ifndef WS2812_PHY_CH582M_H
#define WS2812_PHY_CH582M_H

#include "ws2812_spi.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief CH582M SPI0 PHY 运行上下文 */
typedef struct
{
    uint32_t actual_hz;    /**< 实际 SPI 时钟频率（Hz）—— 分频后精确值 */
    uint8_t divider;       /**< SPI0 时钟分频寄存器值（2..255，值越大频率越低） */
    uint8_t initialized;   /**< 初始化完成标志（1=SPI0 已配置） */
} ws2812_phy_ch582m_t;

#ifdef __cplusplus
}
#endif

#endif /* WS2812_PHY_CH582M_H */
