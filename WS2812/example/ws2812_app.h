/**
* @file ws2812_app.h
* @brief CH582M WS2812 SPI 示例应用对外接口声明
*
* 所属模块：WS2812 驱动示例（演示 SPI 后端 + 颜色插值的完整用法）。
* 对应实现：ws2812_app.c；依赖后端：ws2812_spi.h + ws2812_phy_ch582m.h。
*
* 演示内容：
*   1. SPI 后端 + WS2812 管理层初始化链路；
*   2. 使用整数线性插值在 8 种预设颜色之间平滑过渡；
*   3. 忙状态机的正确用法（is_busy 轮询 + show 触发）；
*   4. CH582M SPI DMA 4 字节对齐要求。
*/
#ifndef WS2812_APP_H
#define WS2812_APP_H

#include "ws2812.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 WS2812 SPI 示例
 *
 * 配置 SPI 后端 + WS2812 管理层 + 清空灯珠 + 重置插值计数器。
 *
 * @return WS2812_OK 成功；WS2812_RET_PARAM / WS2812_RET_IO 等后端返回码
 */
ws2812_ret_t ws2812_spi_example_init(void);

/**
 * @brief  执行一帧颜色插值更新（需以 EXAMPLE_DELAY_MS 节拍调用）
 *
 * 忙时直接返回 BUSY，空闲时计算插值颜色 → 填充全部灯珠 → 触发传输。
 * 每 EXAMPLE_STEPS 帧切换到下一对颜色。
 *
 * @return WS2812_OK 成功；WS2812_RET_BUSY 传输中；其他错误码同 ws2812_fill_hex / show
 */
ws2812_ret_t ws2812_spi_example_step(void);

/**
 * @brief  阻塞式演示入口（init → 死循环 step + DelayMs）
 *
 * 直接调用即可让一个灯珠循环显示 黑→白→红→绿→蓝→黄→青→紫 渐变。
 */
void ws2812_spi_example_0(void);

#ifdef __cplusplus
}
#endif

#endif /* WS2812_APP_H */
