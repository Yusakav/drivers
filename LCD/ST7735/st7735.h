/**
 * @file st7735.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-11-09
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef ST7735_H
#define ST7735_H

#ifdef __cplusplus
extern "C" {
#endif /* Cplusplus */

#include "ch32l103.h"

#define LCD_RGB888 1
#define LCD_RGB565 2

/* 配置当前使用的颜色模式 */
#define LCD_COLOR_TYPE LCD_RGB565

/* 根据配置选择 Color_t 类型 */
#if LCD_COLOR_TYPE == LCD_RGB888
typedef uint32_t Color_t; /**< 24位真彩色 */

// 定义RGB888颜色编码宏（24位颜色：0xRRGGBB）
#define RGB_COLOR(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))

#elif LCD_COLOR_TYPE == LCD_RGB565
typedef uint16_t Color_t; /**< 16位RGB565 */

                          // 定义RGB565颜色编码宏
#define RGB_COLOR(r, g, b) (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))

#else
#error "Unsupported LCD_COLOR_TYPE"
#endif

// 颜色常量定义
#define LCD_COLOR_BLUE RGB_COLOR (0, 0, 255)              // 纯蓝色
#define LCD_COLOR_GREEN RGB_COLOR (0, 255, 0)             // 纯绿色
#define LCD_COLOR_RED RGB_COLOR (255, 0, 0)               // 纯红色
#define LCD_COLOR_CYAN RGB_COLOR (0, 255, 255)            // 青色（绿+蓝）
#define LCD_COLOR_MAGENTA RGB_COLOR (255, 0, 255)         // 洋红色（红+蓝）
#define LCD_COLOR_YELLOW RGB_COLOR (255, 255, 0)          // 黄色（红+绿）
#define LCD_COLOR_LIGHTBLUE RGB_COLOR (128, 128, 255)     // 亮蓝色
#define LCD_COLOR_LIGHTGREEN RGB_COLOR (128, 255, 128)    // 亮绿色
#define LCD_COLOR_LIGHTRED RGB_COLOR (255, 128, 128)      // 亮红色
#define LCD_COLOR_LIGHTCYAN RGB_COLOR (128, 255, 255)     // 亮青色
#define LCD_COLOR_LIGHTMAGENTA RGB_COLOR (255, 128, 255)  // 亮洋红色
#define LCD_COLOR_LIGHTYELLOW RGB_COLOR (255, 255, 128)   // 亮黄色
#define LCD_COLOR_DARKBLUE RGB_COLOR (0, 0, 128)          // 深蓝色
#define LCD_COLOR_DARKGREEN RGB_COLOR (0, 128, 0)         // 深绿色
#define LCD_COLOR_DARKRED RGB_COLOR (128, 0, 0)           // 深红色
#define LCD_COLOR_DARKCYAN RGB_COLOR (0, 128, 128)        // 深青色
#define LCD_COLOR_DARKMAGENTA RGB_COLOR (128, 0, 128)     // 深洋红色
#define LCD_COLOR_DARKYELLOW RGB_COLOR (128, 128, 0)      // 深黄色
#define LCD_COLOR_WHITE RGB_COLOR (255, 255, 255)         // 纯白色
#define LCD_COLOR_LIGHTGRAY RGB_COLOR (211, 211, 211)     // 亮灰色
#define LCD_COLOR_GRAY RGB_COLOR (128, 128, 128)          // 灰色
#define LCD_COLOR_DARKGRAY RGB_COLOR (64, 64, 64)         // 深灰色
#define LCD_COLOR_BLACK RGB_COLOR (0, 0, 0)               // 纯黑色
#define LCD_COLOR_BROWN RGB_COLOR (165, 42, 42)           // 棕色
#define LCD_COLOR_ORANGE RGB_COLOR (255, 165, 0)          // 橙色
#define LCD_COLOR_TRANSPARENT RGB_COLOR (0, 0, 0)         // 透明色


/**
 * @brief  ST7735 Size
 */
#define ST7735_OK (0)
#define ST7735_ERROR (-1)

/**
 * @brief  ST7735 Size
 */
#define ST7735_WIDTH  128U
#define ST7735_HEIGHT 128U


/**
 *  @brief LCD_OrientationTypeDef
 *  Possible values of Display Orientation
 */
#define ST7735_ORIENTATION_PORTRAIT 0x00U         /* Portrait orientation choice of LCD screen               */
#define ST7735_ORIENTATION_PORTRAIT_ROT180 0x01U  /* Portrait rotated 180? orientation choice of LCD screen  */
#define ST7735_ORIENTATION_LANDSCAPE 0x02U        /* Landscape orientation choice of LCD screen              */
#define ST7735_ORIENTATION_LANDSCAPE_ROT180 0x03U /* Landscape rotated 180? orientation choice of LCD screen */

/**
 *  @brief  Possible values of pixel data format (ie color coding)
 */
#define ST7735_FORMAT_RBG444 0x03U /* Pixel format chosen is RGB444 : 12 bpp */
#define ST7735_FORMAT_RBG565 0x05U /* Pixel format chosen is RGB565 : 16 bpp */
#define ST7735_FORMAT_RBG666 0x06U /* Pixel format chosen is RGB666 : 18 bpp */
#define ST7735_FORMAT_DEFAULT ST7735_FORMAT_RBG565


int32_t st7735_init (uint32_t ColorCoding, uint32_t Orientation);
int32_t st7735_deinit (void);
int32_t st7735_write_reg (uint8_t cmd, uint8_t *data, uint32_t len);
int32_t st7735_write_data (uint8_t *data, uint32_t len);

void lcd_set_window (uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_draw_point (uint16_t x, uint16_t y, uint16_t color);
void lcd_fill_rect (uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void lcd_draw_line (uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color);
void lcd_draw_rect (uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color);
void lcd_draw_circle (uint16_t x0, uint16_t y0, uint16_t r, uint32_t color);

/**
 * @brief 绘制填充圆角矩形
 * @param x, y  起始坐标
 * @param w, h  宽度和高度
 * @param r     圆角半径 (建议取 2 或 3)
 * @param color 颜色
 */
void lcd_fill_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t color);


/**
 * @brief 绘制圆角边框 (用于进度条底框)
 */
void lcd_draw_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t color);
/**
 * @brief 在指定位置显示一个字符
 *
 * @param x 起始坐标X轴
 * @param y 起始坐标Y轴
 * @param num 显示字符
 * @param size 字体大小, 可选12/16/24/32
 * @param color 字体颜色
 */
void lcd_show_char(uint16_t x, uint16_t y, uint8_t num, uint8_t size, uint32_t color);

/**
 * @brief 显示一串字符串
 *
 * @param x         起始坐标点X轴。
 * @param y         起始坐标点Y轴。
 * @param width     字符串显示区域长度
 * @param height    字符串显示区域高度
 * @param size      字体大小
 * @param p         要显示的字符串首地址
 * @param color
 */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint32_t color);

/**
 * @brief 显示指定的数字，高位为0的话不显示
 *
 * @param x         起始坐标点X轴。
 * @param y         起始坐标点Y轴。
 * @param num       数值
 * @param len       数字位数。
 * @param size      字体大小
 * @param color
 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint32_t color);

#ifdef __cplusplus
}
#endif /* Cplusplus */

#endif /* ST7735_H */
