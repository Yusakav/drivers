/**
 * @file ascii_font.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-07-30
 * 
 * @copyright 版权所有 (c) 2025 Yusaka
 * 
 */

#ifndef ASCII_FONT_H
#define ASCII_FONT_H

#ifdef __cplusplus
extern "C"
{
#endif /* Cplusplus */

//常用ASCII表
//偏移量32 
//ASCII字符集: !"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~
//PC2LCD2002取模方式设置：阴码+逐列式+顺向+C51格式
//总共：3个字符集（12*12、16*16、24*24和32*32），用户可以自行新增其他分辨率的字符集。
//每个字符所占用的字节数为:(size/8+((size%8)?1:0))*(size/2),其中size:是字库生成时的点阵大小(12/16/24/32...)

//12*12 ASCII字符集点阵
extern const unsigned char asc2_1206[95][12];  

//16*16 ASCII字符集点阵
extern const unsigned char asc2_1608[95][16];  

//24*24 ASICII字符集点阵
extern const unsigned char asc2_2412[95][36];     

//32*32 ASCII字符集点阵
extern const unsigned char asc2_3216[95][128];

extern const unsigned char asc2_4824[95][272];

#ifdef __cplusplus
}
#endif /* Cplusplus */

#endif // ASCII_FONT_H
