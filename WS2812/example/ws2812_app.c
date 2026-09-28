/**
* @file ws2812_app.c
* @brief CH582M WS2812 SPI 示例（整数线性颜色插值）
*
* 所属模块：WS2812 驱动示例（演示 SPI 后端 + 颜色插值的完整用法）。
* 对应头文件：ws2812_app.h；硬件依赖：CH582M SPI + DMA。
*
* 完整初始化链路：
*   ws2812_phy_ch582m_t (SPI PHY 层)
*       ↓
*   ws2812_spi_configure (SPI 后端配置，绑定 DMA 缓冲 + 时序参数)
*       ↓
*   ws2812_config_t (像素数组 + 颜色顺序 + 后端类型 + backend_context)
*       ↓
*   ws2812_init → ws2812_spi_backend_init → SPI DMA + WS2812 管理层就绪
*
* 插值算法：整数线性插值，step 从 0 到 EXAMPLE_STEPS（含），
*           每通道 delta×step / EXAMPLE_STEPS，避免浮点。
*/
#include "ws2812_app.h"

#include "ws2812_phy_ch582m.h"
#include "ws2812_spi.h"
#include "CH58x_common.h"

#include <stddef.h>
#include <stdint.h>

/* ===== 示例参数 ===== */
#define EXAMPLE_LED_COUNT 1U    /**< 灯珠数量（本示例仅 1 颗） */
#define EXAMPLE_STEPS 200U      /**< 每对颜色之间的插值步数（0..200 共 201 个采样点） */
#define EXAMPLE_DELAY_MS 10U    /**< step 调用间隔（毫秒）—— 200×10ms = 2s 走完一对颜色 */

/** @brief SPI DMA 缓冲大小（字节）= ceil(LED_COUNT×24bit / SPI 0/1 码比 / 8) + reset 时间对应的 SPI 字节数 */
#define EXAMPLE_TX_BYTES WS2812_SPI_BUFFER_SIZE(EXAMPLE_LED_COUNT, WS2812_SPI_DEFAULT_HZ, WS2812_DEFAULT_RESET_US)

/** @brief 数组元素数量宏（编译时求值） */
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

/* ===== 全局对象 ===== */
static ws2812_t g_strip;              /**< WS2812 管理层句柄 */
static ws2812_spi_t g_spi_backend;     /**< SPI 后端上下文 */
static ws2812_phy_ch582m_t g_spi_phy;  /**< CH582M SPI 物理层（GPIO/DMA 寄存器操作） */
static ws2812_rgb_t g_pixels[EXAMPLE_LED_COUNT]; /**< 像素缓冲（驱动写入，后端读取） */

/**
 * @brief SPI DMA 源缓冲（必须 4 字节对齐）
 *
 * CH582M SPI DMA 要求源地址 4 字节对齐，所以用 uint32_t 数组。
 * WS2812_SPI_BUFFER_SIZE 计算的是字节数，向上对齐到 uint32_t 个数。
 */
static uint32_t g_tx_words[(EXAMPLE_TX_BYTES + 3U) / 4U];

static uint16_t g_step;        /**< 当前插值步（0..EXAMPLE_STEPS） */
static size_t g_color_index;   /**< 当前颜色对起点索引（指向 g_colors） */

/** @brief 预设颜色表（8 种，插值循环：0→1→2→...→7→0） */
static const uint32_t g_colors[] = {
    0x000000UL,  /**< 黑 */
    0xFFFFFFUL,  /**< 白 */
    0xFF0000UL,  /**< 红 */
    0x00FF00UL,  /**< 绿 */
    0x0000FFUL,  /**< 蓝 */
    0xFFFF00UL,  /**< 黄 */
    0x00FFFFUL,  /**< 青 */
    0xFF00FFUL,  /**< 紫 */
};

/* ===== 内部辅助函数 ===== */

/**
 * @brief  两通道整数线性插值（避免浮点）
 *
 * formula: result = from + (to - from) × step / EXAMPLE_STEPS
 *
 * @param from  起始通道值（0..255）
 * @param to  目标通道值（0..255）
 * @param step  当前步（0..EXAMPLE_STEPS，step=0 返回 from，step=STEPS 返回 to）
 * @return 插值结果（0..255）
 */
static uint8_t interpolate_channel(uint8_t from, uint8_t to, uint16_t step)
{
    int32_t delta = (int32_t)to - (int32_t)from;
    return (uint8_t)((int32_t)from + (delta * (int32_t)step) / (int32_t)EXAMPLE_STEPS);
}

/**
 * @brief  两个 24 位颜色之间的三通道整数线性插值
 * @param from  起始颜色（0xRRGGBB）
 * @param to  目标颜色（0xRRGGBB）
 * @param step  当前步
 * @return 插值结果（0xRRGGBB）
 */
static uint32_t interpolate_color(uint32_t from, uint32_t to, uint16_t step)
{
    uint8_t r = interpolate_channel((uint8_t)(from >> 16U), (uint8_t)(to >> 16U), step);
    uint8_t g = interpolate_channel((uint8_t)(from >> 8U), (uint8_t)(to >> 8U), step);
    uint8_t b = interpolate_channel((uint8_t)from, (uint8_t)to, step);
    return ((uint32_t)r << 16U) | ((uint32_t)g << 8U) | b;
}

/* ===== 对外接口 ===== */

/**
 * @brief  初始化 WS2812 SPI 示例
 *
 * 配置链路：SPI 物理层 → SPI 后端（绑定 DMA 缓冲）→ WS2812 管理层。
 * 成功后清屏 + 重置插值计数器，处于可 step 状态。
 *
 * @return WS2812_OK / 后端返回码
 */
ws2812_ret_t ws2812_spi_example_init(void)
{
    ws2812_spi_config_t spi_config = {
        &g_spi_phy, (uint8_t *)g_tx_words, sizeof(g_tx_words), WS2812_SPI_DEFAULT_HZ, WS2812_DEFAULT_RESET_US,
    };
    ws2812_config_t config;
    ws2812_ret_t ret;

    /* ① 配置 SPI 后端：DMA 缓冲 + 时序参数 */
    ret = ws2812_spi_configure(&g_spi_backend, &spi_config, EXAMPLE_LED_COUNT);
    if (ret != WS2812_OK)
    {
        return ret;
    }

    /* ② 配置 WS2812 管理层：像素缓冲 + 后端上下文 */
    config.pixels = g_pixels;
    config.pixel_count = EXAMPLE_LED_COUNT;
    config.color_order = WS2812_ORDER_GRB;
    config.backend = WS2812_BACKEND_SPI;
    config.backend_context = &g_spi_backend;
    ret = ws2812_init(&g_strip, &config);
    if (ret != WS2812_OK)
    {
        return ret;
    }

    /* ③ 重置插值状态 + 清屏 */
    g_step = 0U;
    g_color_index = 0U;
    return ws2812_clear_and_show(&g_strip);
}

/**
 * @brief  执行一帧颜色插值更新（需以 EXAMPLE_DELAY_MS 节拍调用）
 *
 * 忙状态机：is_busy==1 → 返回 BUSY，不消费 DMA 完成事件；
 *           is_busy==0 → take_complete 消费挂起完成事件（防下次误判）；
 *           计算插值 → fill_hex → show → step++；
 *           step>STEPS 时 step 归零、color_index 推进到下一对颜色。
 *
 * @return WS2812_OK / BUSY / 其他错误码
 */
ws2812_ret_t ws2812_spi_example_step(void)
{
    size_t next_index;
    uint32_t color;
    ws2812_ret_t ret;

    /* ① 忙检测：DMA 发送中直接跳过 */
    if (ws2812_is_busy(&g_strip) != 0U)
    {
        return WS2812_RET_BUSY;
    }
    /* ② 消费上次 DMA 完成事件（防 complete_pending 残留） */
    (void)ws2812_take_complete(&g_strip);

    /* ③ 计算插值颜色（从 g_colors[color_index] → g_colors[next_index]，当前步 g_step） */
    next_index = (g_color_index + 1U) % ARRAY_SIZE(g_colors);
    color = interpolate_color(g_colors[g_color_index], g_colors[next_index], g_step);
    ret = ws2812_fill_hex(&g_strip, color);
    if (ret != WS2812_OK)
    {
        return ret;
    }

    /* ④ 触发 SPI DMA 传输 */
    ret = ws2812_show(&g_strip);
    if (ret != WS2812_OK)
    {
        return ret;
    }

    /* ⑤ 推进插值步，超限则切换到下一对颜色 */
    ++g_step;
    if (g_step > EXAMPLE_STEPS)
    {
        g_step = 0U;
        g_color_index = next_index;
    }
    return WS2812_OK;
}

/**
 * @brief  阻塞式演示入口（init → 死循环 step + DelayMs）
 *
 * 直接调用即可让一个灯珠循环显示 黑→白→红→绿→蓝→黄→青→紫 渐变。
 * 201 帧×10ms/帧 ≈ 2s 走完一对颜色，8 色循环一周约 14s。
 */
void ws2812_spi_example_0(void)
{
    if (ws2812_spi_example_init() != WS2812_OK)
    {
        return;
    }
    for (;;)
    {
        (void)ws2812_spi_example_step();
        DelayMs(EXAMPLE_DELAY_MS);
    }
}
