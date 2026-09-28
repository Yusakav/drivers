/**
* @file ws2812_phy_ch582m.c
* @brief CH582M SPI0 PHY 层实现（覆盖 ws2812_spi.h 中的弱符号）
*
* 所属模块：WS2812 LED 驱动（平台 PHY 层，绑定 CH582M SPI0 + DMA）。
* 对应头文件：ws2812_phy_ch582m.h；弱符号声明：ws2812_spi.h。
*
* 本文件实现：
*   - ws2812_spi_phy_init：GPIO 重映射 → PB13/PB14 推挽输出 → SPI0 主模式 → 时钟分频 → Mode3 高位在前；
*   - ws2812_spi_phy_transmit：参数校验（含 4 字节对齐）→ SPI0_MasterDMATrans → 立即调 ws2812_transfer_complete。
*
* 分频公式：divider = ceil(source_hz / target_hz)（四舍五入，避免 target_hz 因截断过低）。
* divider 必须在 [2, UINT8_MAX] 范围内（SPI0 硬件约束）。
*
* 注意：CH582M SPI DMA 要求源地址 4 字节对齐，ws2812_spi_phy_transmit 中有显式对齐校验。
*/
#include "ws2812_phy_ch582m.h"

#include "CH58x_common.h"

#include <limits.h>
#include <stdint.h>

/**
 * @brief  SPI 物理层初始化（覆盖 ws2812_spi.h 中的弱符号）
 *
 * 配置链路：GetSysClock → divider 计算 → GPIO 重映射 PB13/PB14 → SPI0 主模式 → 分频 → Mode3。
 * divider 超出 [2, UINT8_MAX] 或 target_hz=0 时返回 -1。
 *
 * @param user  PHY 上下文（ws2812_phy_ch582m_t*，必须非 NULL）
 * @param target_hz  目标 SPI 时钟频率（Hz，必须非 0）
 * @return 0=成功；-1=失败
 */
int ws2812_spi_phy_init(void *user, uint32_t target_hz)
{
    ws2812_phy_ch582m_t *phy = (ws2812_phy_ch582m_t *)user;
    uint32_t source_hz;
    uint32_t divider;

    if ((phy == NULL) || (target_hz == 0U)) {
        return -1;
    }
    source_hz = GetSysClock();
    /* 分频 = ceil(source_hz / target_hz)：加 target_hz/2 实现四舍五入 */
    divider = (source_hz + (target_hz / 2U)) / target_hz;
    if ((divider < 2U) || (divider > UINT8_MAX)) {
        return -1;
    }

    /* GPIO 重映射 PB13(SPI0_SCK) PB14(SPI0_MOSI) */
    GPIOPinRemap(ENABLE, RB_PIN_SPI0);
    GPIOB_ModeCfg(GPIO_Pin_13 | GPIO_Pin_14, GPIO_ModeOut_PP_5mA);
    /* SPI0 主模式默认配置 */
    SPI0_MasterDefInit();
    /* 设置分频寄存器（值越大频率越低） */
    SPI0_CLKCfg((uint8_t)divider);
    /* Mode3=CPOL高/CPHA沿1，高位在前 */
    SPI0_DataMode(Mode3_HighBitINFront);

    phy->divider = (uint8_t)divider;
    phy->actual_hz = source_hz / divider;
    phy->initialized = 1U;
    return 0;
}

/**
 * @brief  SPI DMA 异步发送（覆盖 ws2812_spi.h 中的弱符号）
 *
 * 启动 SPI0 DMA 传输后立即调 ws2812_transfer_complete(owner) 通知管理层。
 * CH582M SPI DMA 完成中断尚未配置，这里在 DMA 启动后立即上报完成。
 *
 * 校验项：phy 非 NULL 且 initialized；data 非 NULL；length 合法；owner 非 NULL；
 *        data 地址 4 字节对齐（CH582M SPI DMA 硬件约束）。
 *
 * @param user  PHY 上下文
 * @param data  DMA 源缓冲（必须 4 字节对齐，uintptr_t & 0x03 == 0）
 * @param length  发送长度（字节，≤ UINT16_MAX）
 * @param owner  WS2812 管理层句柄（DMA 完成回调时传入）
 * @return 0=成功启动；-1=失败
 */
int ws2812_spi_phy_transmit(void *user, const uint8_t *data, size_t length,
                            ws2812_t *owner)
{
    ws2812_phy_ch582m_t *phy = (ws2812_phy_ch582m_t *)user;

    if ((phy == NULL) || (phy->initialized == 0U) || (data == NULL) ||
        (length == 0U) || (length > UINT16_MAX) || (owner == NULL)) {
        return -1;
    }
    /* CH582M SPI DMA 要求源地址 4 字节对齐 */
    if (((uintptr_t)data & 0x03U) != 0U) {
        return -1;
    }

    SPI0_MasterDMATrans((uint8_t *)(uintptr_t)data, (uint16_t)length);
    ws2812_transfer_complete(owner);
    return 0;
}
