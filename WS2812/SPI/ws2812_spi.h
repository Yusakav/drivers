/**
 * @file app_ws2812.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief WS2812 LED控制接口
 * @version 0.1
 * @date 2026-04-16
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#ifndef APP_WS2812_H
#define APP_WS2812_H

#include "ws2812_phy_ch582m.h"

#define APP_WS2812_LED_NUM  1  // LED数量
#define APP_WS2812_MAX_STEP (200)  // 最大步长

#define APP_WS2812_PIXEL_PRE_LEN (9u)  // 每个像素的预冲宽度
#define APP_WS2812_PIXEL_RESET_LEN (19u)  // 每个像素的重置宽度
#define APP_WS2812_COLOR_BUFFER_LEN (((APP_WS2812_LED_NUM)*APP_WS2812_PIXEL_PRE_LEN)+APP_WS2812_PIXEL_RESET_LEN)  // 颜色缓冲区长度


/**
 * @brief 设置WS2812 LED颜色
 * 
 * @param led_id LED ID
 * @param r 红色
 * @param g 绿色
 * @param b 蓝色
 */
void app_ws2812_set_color_rgb(uint16_t led_id, uint8_t r, uint8_t g, uint8_t b);


/**
 * @brief 设置WS2812 LED颜色
 * 
 * @param led_id LED ID
 * @param hex 颜色值，格式为0xRRGGBB
 */
void app_ws2812_set_color_hex(uint16_t led_id, uint32_t hex);

/**
 * @brief  WS2812 LED颜色渐变示例
 * 
 * @note 该示例将所有LED颜色渐变，每个LED颜色渐变到下一个颜色
 * 
 */
void app_ws2812_example_0(void);

/**
 * @brief 初始化WS2812 LED
 * 
 */
void app_ws2812_init(void);

/**
 * @brief 同步WS2812 LED颜色
 * 
 */
void w2812_sync(void);




#endif  /* APP_WS2812_H */
