/**
* @file ws2812.c
* @brief 平台无关的 WS2812 像素管理实现
*
* 所属模块：WS2812 LED 驱动（像素管理层，不直接操作硬件寄存器）。
* 对应头文件：ws2812.h；依赖：ws2812_spi.h / ws2812_pwm.h。
*
* 本文件实现：
*   - 设备生命周期管理（ws2812_init + 后端 init + 像素缓冲清零）；
*   - 像素缓冲读写（set_pixel_rgb/hex / fill_rgb/hex / clear）；
*   - 后端调度与忙状态机（show → busy=1 → 后端 start → transfer_complete → busy=0）；
*   - 原子性完成标志（complete_pending + take_complete）；
*   - 修改+show 组合便捷接口（5 个 *_and_show 函数，复用基础接口）。
*
* 注意：所有后端上下文访问通过 void* + 强制类型转换实现，与 SPI/PWM 后端解耦。
*       busy / complete_pending 声明为 volatile，因为后端完成中断会异步写入。
*/
#include "ws2812.h"
#include "PWM/ws2812_pwm.h"
#include "SPI/ws2812_spi.h"

#include <string.h>

/* ===== 内部辅助函数 ===== */

/**
 * @brief  校验颜色顺序枚举值是否在合法范围内
 * @param order  颜色顺序
 * @return 1=合法；0=非法
 */
static uint8_t order_valid(ws2812_color_order_t order)
{
    return (uint8_t)(order <= WS2812_ORDER_BGR);
}

/**
 * @brief  统一校验设备句柄是否就绪（非空 + 已初始化 + 指针有效 + backend 合法）
 *
 * 本层几乎所有对外函数都先调 check_ready。ws2812_show 额外检查 busy。
 *
 * @param dev  设备句柄
 * @return WS2812_OK 就绪；WS2812_RET_NULL / PARAM
 */
static ws2812_ret_t check_ready(const ws2812_t *dev)
{
    if (dev == NULL)
    {
        return WS2812_RET_NULL;
    }
    if ((dev->initialized == 0U) || (dev->pixels == NULL) || (dev->backend_context == NULL) ||
        (dev->backend > WS2812_BACKEND_PWM))
    {
        return WS2812_RET_PARAM;
    }
    return WS2812_OK;
}

/* ===== 传输完成回调（ISR 中调用） ===== */

/**
 * @brief  传输完成回调（由后端 DMA/SPI 完成中断调用）
 *
 * 清除 busy 标志，置位 complete_pending。ISR 中安全，内部无阻塞操作。
 * 若 dev 为空或 busy 已为 0（重复调用），直接 return 忽略。
 *
 * @param dev  设备句柄
 */
void ws2812_transfer_complete(ws2812_t *dev)
{
    if ((dev == NULL) || (dev->busy == 0U))
    {
        return;
    }
    dev->busy = 0U;
    dev->complete_pending = 1U;
}

/* ===== 生命周期 ===== */

/**
 * @brief  初始化 WS2812 设备句柄
 *
 * 校验 config → memset(dev,0) → 拷贝字段 → 调后端 init → 清空像素缓冲 → initialized=1。
 * 后端 init 失败时回滚 memset(dev,0)，保证 dev 处于可重新 init 的干净状态。
 *
 * @param dev  输出设备句柄
 * @param config  初始化配置（pixels/backend_context 必须非 NULL）
 * @return WS2812_OK / RET_NULL / RET_PARAM / 后端返回码
 */
ws2812_ret_t ws2812_init(ws2812_t *dev, const ws2812_config_t *config)
{
    ws2812_ret_t ret;

    if ((dev == NULL) || (config == NULL))
    {
        return WS2812_RET_NULL;
    }
    if ((config->pixels == NULL) || (config->pixel_count == 0U) || (config->backend_context == NULL) ||
        (config->backend > WS2812_BACKEND_PWM) || !order_valid(config->color_order))
    {
        return WS2812_RET_PARAM;
    }

    memset(dev, 0, sizeof(*dev));
    dev->pixels = config->pixels;
    dev->pixel_count = config->pixel_count;
    dev->color_order = config->color_order;
    dev->backend = config->backend;
    dev->backend_context = config->backend_context;

    switch (dev->backend)
    {
    case WS2812_BACKEND_SPI:
        ret = ws2812_spi_backend_init((ws2812_spi_t *)dev->backend_context);
        break;
    case WS2812_BACKEND_PWM:
        ret = ws2812_pwm_backend_init((ws2812_pwm_t *)dev->backend_context);
        break;
    default:
        ret = WS2812_RET_PARAM;
        break;
    }
    if (ret != WS2812_OK)
    {
        memset(dev, 0, sizeof(*dev));
        return ret;
    }

    memset(dev->pixels, 0, (size_t)dev->pixel_count * sizeof(dev->pixels[0]));
    dev->initialized = 1U;
    return WS2812_OK;
}

/* ===== 像素修改 ===== */

/**
 * @brief  设置单个像素的 RGB 值（不触发传输）
 * @param dev  已初始化设备句柄
 * @param index  像素索引（0 基）
 * @param r/g/b  RGB 值（0..255）
 * @return WS2812_OK / RET_RANGE / RET_PARAM / RET_NULL
 */
ws2812_ret_t ws2812_set_pixel_rgb(ws2812_t *dev, uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_ret_t ret = check_ready(dev);

    if (ret != WS2812_OK)
    {
        return ret;
    }
    if (index >= dev->pixel_count)
    {
        return WS2812_RET_RANGE;
    }
    dev->pixels[index].r = r;
    dev->pixels[index].g = g;
    dev->pixels[index].b = b;
    return WS2812_OK;
}

/**
 * @brief  设置单个像素（24 位十六进制颜色 0xRRGGBB）
 * @param dev  已初始化设备句柄
 * @param index  像素索引
 * @param rgb  24 位颜色值
 * @return 同 ws2812_set_pixel_rgb
 */
ws2812_ret_t ws2812_set_pixel_hex(ws2812_t *dev, uint16_t index, uint32_t rgb)
{
    return ws2812_set_pixel_rgb(dev, index, (uint8_t)((rgb >> 16) & 0xFFU), (uint8_t)((rgb >> 8) & 0xFFU),
                                (uint8_t)(rgb & 0xFFU));
}

/**
 * @brief  将全部像素填充为指定 RGB 值（不触发传输）
 * @param dev  已初始化设备句柄
 * @param r/g/b  RGB 值
 * @return WS2812_OK / RET_PARAM / RET_NULL
 */
ws2812_ret_t ws2812_fill_rgb(ws2812_t *dev, uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t i;
    ws2812_ret_t ret = check_ready(dev);

    if (ret != WS2812_OK)
    {
        return ret;
    }
    for (i = 0U; i < dev->pixel_count; ++i)
    {
        dev->pixels[i].r = r;
        dev->pixels[i].g = g;
        dev->pixels[i].b = b;
    }
    return WS2812_OK;
}

/**
 * @brief  将全部像素填充为指定十六进制颜色（不触发传输）
 * @param dev  已初始化设备句柄
 * @param rgb  24 位颜色值
 * @return 同 ws2812_fill_rgb
 */
ws2812_ret_t ws2812_fill_hex(ws2812_t *dev, uint32_t rgb)
{
    return ws2812_fill_rgb(dev, (uint8_t)((rgb >> 16) & 0xFFU), (uint8_t)((rgb >> 8) & 0xFFU), (uint8_t)(rgb & 0xFFU));
}

/**
 * @brief  清空全部像素（填充黑色 0,0,0，不触发传输）
 * @param dev  已初始化设备句柄
 * @return 同 ws2812_fill_rgb
 */
ws2812_ret_t ws2812_clear(ws2812_t *dev)
{
    return ws2812_fill_rgb(dev, 0U, 0U, 0U);
}

/* ===== 传输控制 ===== */

/**
 * @brief  触发一次后端传输（异步，立即返回）
 *
 * 忙状态机：busy=0 → 置 busy=1, complete_pending=0 → 调后端 start → 后端 DMA 完成中断 →
 *           transfer_complete → busy=0, complete_pending=1 → 主循环 take_complete 清零。
 * 后端 start 返回非 OK 时，立即清 busy，可立即重试。
 *
 * @param dev  已初始化设备句柄
 * @return WS2812_OK / RET_BUSY / RET_PARAM / RET_NULL / 后端返回码
 */
ws2812_ret_t ws2812_show(ws2812_t *dev)
{
    ws2812_ret_t ret = check_ready(dev);

    if (ret != WS2812_OK)
    {
        return ret;
    }
    if (dev->busy != 0U)
    {
        return WS2812_RET_BUSY;
    }

    dev->busy = 1U;
    dev->complete_pending = 0U;
    switch (dev->backend)
    {
    case WS2812_BACKEND_SPI:
        ret = ws2812_spi_backend_start((ws2812_spi_t *)dev->backend_context, dev->pixels, dev->pixel_count,
                                       dev->color_order, dev);
        break;
    case WS2812_BACKEND_PWM:
        ret = ws2812_pwm_backend_start((ws2812_pwm_t *)dev->backend_context, dev->pixels, dev->pixel_count,
                                       dev->color_order, dev);
        break;
    default:
        ret = WS2812_RET_PARAM;
        break;
    }
    if (ret != WS2812_OK)
    {
        dev->busy = 0U;
    }
    return ret;
}

/**
 * @brief  查询传输是否忙
 * @param dev  设备句柄
 * @return 1=后端传输中；0=空闲（dev 为 NULL 也返回 0）
 */
uint8_t ws2812_is_busy(const ws2812_t *dev)
{
    return (dev != NULL) ? dev->busy : 0U;
}

/**
 * @brief  原子性取出并清除 complete_pending 标志
 *
 * 典型用法：ISR 中 transfer_complete 置位 → 主循环 take_complete 取出并清零。
 *
 * @param dev  设备句柄
 * @return 1=自上次取出以来有传输完成；0=未完成
 */
uint8_t ws2812_take_complete(ws2812_t *dev)
{
    uint8_t pending;

    if (dev == NULL)
    {
        return 0U;
    }
    pending = dev->complete_pending;
    dev->complete_pending = 0U;
    return pending;
}

/* ===== 便捷接口：修改 + show 一步完成 ===== */
/* 策略：先调 set/fill/clear，若 OK 则调 show。show 失败直接透传错误码。 */

ws2812_ret_t ws2812_set_pixel_rgb_and_show(ws2812_t *dev, uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_ret_t ret = ws2812_set_pixel_rgb(dev, index, r, g, b);
    return (ret == WS2812_OK) ? ws2812_show(dev) : ret;
}

ws2812_ret_t ws2812_set_pixel_hex_and_show(ws2812_t *dev, uint16_t index, uint32_t rgb)
{
    ws2812_ret_t ret = ws2812_set_pixel_hex(dev, index, rgb);
    return (ret == WS2812_OK) ? ws2812_show(dev) : ret;
}

ws2812_ret_t ws2812_fill_rgb_and_show(ws2812_t *dev, uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_ret_t ret = ws2812_fill_rgb(dev, r, g, b);
    return (ret == WS2812_OK) ? ws2812_show(dev) : ret;
}

ws2812_ret_t ws2812_fill_hex_and_show(ws2812_t *dev, uint32_t rgb)
{
    ws2812_ret_t ret = ws2812_fill_hex(dev, rgb);
    return (ret == WS2812_OK) ? ws2812_show(dev) : ret;
}

ws2812_ret_t ws2812_clear_and_show(ws2812_t *dev)
{
    ws2812_ret_t ret = ws2812_clear(dev);
    return (ret == WS2812_OK) ? ws2812_show(dev) : ret;
}
