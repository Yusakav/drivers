/**
 * @file ws2812_app.c
 * @author Yusakav (YusakaVivy@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2026-09-27
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#include "ws2812_spi.h"

/**
 * @brief 颜色列表
 *
 * @note 颜色列表中包含多个颜色值，用于渐变显示
 *
 */
uint32_t color_list[] = {
    0x00000000, // 黑色
    0x00FFFFFF, // 白色
    0x00FF0000, // 绿色
    0x0000FF00, // 红色
    0x000000FF, // 蓝色
    0x00FFFF00, // 黄色
    0x0000FFFF, // 品红/紫色
    0x00FF00FF, // 青色
    0x0064FF00, // 橙色
    0x00FFC0CB, // 粉色
    0x00FFD700, // 金色
    0x00EE82EE, // 紫罗兰
    0x009ACD32, // 黄绿色
    0x004B0082, // 靛蓝色
    0x00008080, // 蓝绿色
    0x00800080, // 紫色
    0x00FF4500, // 橙红
    0x0032CD32, // 酸橙绿
    0x001E90FF, // 道奇蓝
    0x00FF1493, // 深粉色
    0x00DC143C, // 深红
    0x008B4513, // 棕色
    0x0040E0D0, // 绿松石
    0x00EE7600, // 暗橙色
    0x007FFF00, // 绿黄色
    0x009932CC, // 深紫色
    0x0000CED1, // 暗青色
    0x00BC8F8F, // 浅棕色
    0x00DDA0DD, // 梅红色
    0x00F0E68C, // 卡其色
};

uint8_t *str_list[] = {"黑色",
                       "白色",
                       "绿色",
                       "红色",
                       "蓝色",
                       "黄色",
                       "品红",
                       "青色",
                       "橙色",
                       "粉色",
                       "金色",
                       "紫罗兰",
                       "黄绿色",
                       "靛蓝色",
                       "蓝绿色",
                       "紫色",
                       "橙红",
                       "酸橙绿",
                       "道奇蓝",
                       "深粉色",
                       "深红",
                       "棕色",
                       "绿松石",
                       "暗橙色",
                       "绿黄色",
                       "深紫色",
                       "暗青色",
                       "浅棕色",
                       "梅红色",
                       "卡其色",
};


/**
 * @brief  WS2812 LED颜色渐变示例
 *
 * @note 该示例将所有LED颜色渐变，每个LED颜色渐变到下一个颜色
 *
 */
void ws2812_spi_example_0(void)
{

    uint32_t i = 0, j = 0;
    uint32_t c = 0x0f0f00;

    while (1)
    {
        c = color_list[i];
        uint32_t next_color = color_list[i + 1];
        i++;
        if ((i + 1) >= LIST_SIZE(color_list))
        {
            i = 0;
        }
        USB_PRINT("%s\n", str_list[i]);
        for (j = 0; j < APP_WS2812_MAX_STEP; j += 1)
        {
            uint32_t color = interpolateColors(c, next_color, j);
            for (int var = 0; var < APP_WS2812_LED_NUM; ++var)
            {
                setPixelColor(var, hex2rgb(color));
            }
            w2812_spi_sync();
            DelayMs(100);
        }
    }
}
