/**
* @file test_ch582m_phy.c
* @brief ws2812_phy_ch582m 单元测试（spi_phy_init 分频计算 + spi_phy_transmit 对齐校验）
*
* 所属模块：WS2812 单元测试（mock CH582M HAL 验证 PHY 层逻辑）。
* 对应被测代码：ws2812_phy_ch582m.c。
*
* Mock 方式：
*   - 覆盖 GetSysClock / GPIOPinRemap / GPIOB_ModeCfg / SPI0_* 函数；
*   - 全局变量 g_divider / g_length / g_dma_calls / g_complete_calls 记录调用；
*   - ws2812_transfer_complete 重定义为直接操作 owner 的 busy 标志（验证 transmit 是否正确回调）。
*
* 测试用例覆盖：
*   1. init(2.4MHz) → divider=25, actual_hz=2.4MHz（60MHz/25）；
*   2. transmit 4 字节对齐缓冲 → DMA 调用 + complete 回调；
*   3. transmit 非对齐缓冲(+1) → 应返回非 0；
*   4. transmit 长度 65536 → 应返回非 0（uint16_max 溢出）。
*/
#include "ws2812_phy_ch582m.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/* ===== Mock 全局变量 ===== */
static uint8_t g_divider;        /**< 记录 SPI0_CLKCfg 收到的分频值 */
static uint16_t g_length;        /**< 记录 SPI0_MasterDMATrans 收到的长度 */
static unsigned g_dma_calls;     /**< DMA 启动次数 */
static unsigned g_complete_calls; /**< transfer_complete 被调用次数 */

/* ===== Mock ws2812_transfer_complete（覆盖 ws2812.c 中的弱符号） ===== */
void ws2812_transfer_complete(ws2812_t *dev)
{
    assert(dev != NULL);
    dev->busy = 0U;
    dev->complete_pending = 1U;
    ++g_complete_calls;
}

/* ===== Mock CH582M HAL 函数 ===== */
uint32_t GetSysClock(void)
{
    return 60000000UL;  /**< 固定系统时钟 60MHz */
}
void GPIOPinRemap(uint8_t enable, uint16_t pin)
{
    assert(enable == 1U && pin == 2U);  /**< SPI0 重映射 pin=2（CH582M RB_PIN_SPI0） */
}
void GPIOB_ModeCfg(uint32_t pins, uint8_t mode)
{
    assert(pins == 0x6000U && mode == 5U);  /**< PB13 | PB14 = 0x6000, GPIO_ModeOut_PP_5mA = 5 */
}
void SPI0_MasterDefInit(void)
{
    /* 空实现：PHY init 会调用但不检查效果 */
}
void SPI0_CLKCfg(uint8_t divider)
{
    g_divider = divider;  /**< 记录分频值供断言 */
}
void SPI0_DataMode(uint8_t mode)
{
    assert(mode == 3U);  /**< Mode3 = CPOL高/CPHA沿1 */
}
void SPI0_MasterDMATrans(uint8_t *data, uint16_t length)
{
    assert(data != NULL);
    ++g_dma_calls;
    g_length = length;
}

/* ===== 测试入口 ===== */
int main(void)
{
    ws2812_phy_ch582m_t phy = {0};
    ws2812_t owner = {0};
    uint32_t aligned_words[4] = {0};  /**< 4 字节对齐的 DMA 源缓冲 */
    uint8_t *aligned = (uint8_t *)aligned_words;

    /* 测试 1：正常初始化 2.4MHz SPI
     * divider = ceil(60MHz / 2.4MHz) = ceil(25) = 25
     * actual_hz = 60MHz / 25 = 2.4MHz */
    assert(ws2812_spi_phy_init(&phy, 2400000UL) == 0);
    assert(g_divider == 25U);
    assert(phy.actual_hz == 2400000UL);

    /* 测试 2：发送对齐缓冲，应成功并回调 complete */
    owner.busy = 1U;
    assert(ws2812_spi_phy_transmit(&phy, aligned, 16U, &owner) == 0);
    assert(g_dma_calls == 1U && g_length == 16U && g_complete_calls == 1U);

    /* 测试 3：发送非对齐缓冲（aligned + 1 偏移），应返回非 0 */
    assert(ws2812_spi_phy_transmit(&phy, aligned + 1U, 4U, &owner) != 0);

    /* 测试 4：发送长度溢出 uint16_t_max，应返回非 0 */
    assert(ws2812_spi_phy_transmit(&phy, aligned, 65536U, &owner) != 0);
    return 0;
}
