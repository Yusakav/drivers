/**
 * @file ws2812_spi.c
 * @author Yusakav (YusakaVivy@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2026-09-27
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#include "ws2812_spi.h"


uint8_t color_buf[APP_WS2812_COLOR_BUFFER_LEN] = {0};

#define LIST_SIZE(list) (sizeof(list) / sizeof(list[0]))
#define hex2rgb(c) (((c) >> 16) & 0xff), (((c) >> 8) & 0xff), ((c) & 0xff)

/**
 * @brief 将8位颜色值转换为16位颜色值
 *
 * @param res 输出缓冲区
 * @param color 输入颜色值
 * @note 输入颜色值为8位，输出为24位, 每个位为110(0x06)或100(0x04)
 */
void convToBit(uint8_t *res, uint8_t color)
{
    uint32_t result = 0;

    result |= (color & 0x80) ? 0xC00000 : 0x800000;
    result |= (color & 0x40) ? 0x180000 : 0x100000;
    result |= (color & 0x20) ? 0x030000 : 0x020000;
    result |= (color & 0x10) ? 0x006000 : 0x004000;
    result |= (color & 0x08) ? 0x000C00 : 0x000800;
    result |= (color & 0x04) ? 0x000180 : 0x000100;
    result |= (color & 0x02) ? 0x000030 : 0x000020;
    result |= (color & 0x01) ? 0x000006 : 0x000004;

    res[0] = (result >> 16) & 0xFF;
    res[1] = (result >> 8) & 0xFF;
    res[2] = result & 0xFF;
}

/**
 * @brief 将RGB颜色值转换为SPI位
 *
 * @param buf 输出缓冲区
 * @param r 红色
 * @param g 绿色
 * @param b 蓝色
 */
void colorToBit(uint8_t *buf, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *res = buf;
    convToBit(res, g);
    convToBit(&(res[3]), r);
    convToBit(&(res[6]), b);
}

/**
 * @brief 设置像素颜色
 *
 * @param index 像素索引
 * @param r 红色
 * @param g 绿色
 * @param b 蓝色
 */
void setPixelColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *buf = &(color_buf[index * APP_WS2812_PIXEL_PRE_LEN]);
    colorToBit(buf, r, g, b);
}

/**
 * @brief 插值颜色
 *
 * @param color1 起始颜色
 * @param color2 结束颜色
 * @param step 插值步长
 * @return uint32_t 插值后的颜色值
 */
uint32_t interpolateColors(uint32_t color1, uint32_t color2, uint8_t step)
{
    uint8_t r1 = (color1 >> 16) & 0xFF;
    uint8_t g1 = (color1 >> 8) & 0xFF;
    uint8_t b1 = color1 & 0xFF;

    uint8_t r2 = (color2 >> 16) & 0xFF;
    uint8_t g2 = (color2 >> 8) & 0xFF;
    uint8_t b2 = color2 & 0xFF;

    float tmp1 = (r2 - r1) * (step / (float)APP_WS2812_MAX_STEP);
    float tmp2 = (g2 - g1) * (step / (float)APP_WS2812_MAX_STEP);
    float tmp3 = (b2 - b1) * (step / (float)APP_WS2812_MAX_STEP);

    uint8_t r = (uint8_t)(r1 + tmp1);
    uint8_t g = (uint8_t)(g1 + tmp2);
    uint8_t b = (uint8_t)(b1 + tmp3);

    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}



/**
 * @brief 同步WS2812 LED颜色
 *
 */
void w2812_sync(void)
{
    SPI0_MasterDMATrans(color_buf, APP_WS2812_COLOR_BUFFER_LEN);
}

/**
 * @brief 初始化WS2812 LED
 *
 */
void app_ws2812_init(void)
{
    uint8_t i = 0;

    GPIOPinRemap(ENABLE, RB_PIN_SPI0);
    GPIOB_ModeCfg(GPIO_Pin_13 | GPIO_Pin_14, GPIO_ModeOut_PP_5mA);
    SPI0_MasterDefInit();
    SPI0_CLKCfg(20);
    SPI0_DataMode(Mode3_HighBitINFront);
    for (i = 0; i < APP_WS2812_LED_NUM; i++)
    {
        setPixelColor(i, 0, 0, 0);
    }
    // setPixelColor (0, 0, 0, 255);
    // w2812_sync();

    // DelayMs(100);
}

/**
 * @brief 设置WS2812 LED颜色
 *
 * @param led_id LED ID
 * @param r 红色
 * @param g 绿色
 * @param b 蓝色
 */
void app_ws2812_set_color_rgb(uint16_t led_id, uint8_t r, uint8_t g, uint8_t b)
{
    if (led_id < APP_WS2812_LED_NUM)
    {
        setPixelColor(led_id, r, g, b);
        w2812_sync();
    }
}

/**
 * @brief 设置WS2812 LED颜色
 *
 * @param led_id LED ID
 * @param hex 颜色值，格式为0xRRGGBB
 */
void app_ws2812_set_color_hex(uint16_t led_id, uint32_t hex)
{
    if (led_id < APP_WS2812_LED_NUM)
    {
        uint8_t r = (hex >> 16) & 0xFF;
        uint8_t g = (hex >> 8) & 0xFF;
        uint8_t b = hex & 0xFF;
        setPixelColor(led_id, r, g, b);
        w2812_sync();
    }
}
