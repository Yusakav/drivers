/**
 * @file soft_spi.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-12-31
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef __SOFT_SPI_H
#define __SOFT_SPI_H

#include <stdint.h>

/* SPI 模式定义 (CPOL & CPHA) */
#define SPI_MODE_0      0x0000  // CPOL=0, CPHA=0
#define SPI_MODE_1      0x0001  // CPOL=0, CPHA=1
#define SPI_MODE_2      0x0002  // CPOL=1, CPHA=0
#define SPI_MODE_3      0x0003  // CPOL=1, CPHA=1

/* 位序定义 */
#define SPI_MSB_FIRST   0x0000  // 高位在前
#define SPI_LSB_FIRST   0x0004  // 低位在前

/* 片选极性定义 */
#define SPI_CS_LOW      0x0000  // 片选低电平有效
#define SPI_CS_HIGH     0x0008  // 片选高电平有效

#define SPI_CS_NONE     0x0020  /**< 传输时不操作 CS 引脚 */

/* 数据宽度定义 */
#define SPI_DATA_MASK      0x0F00   // 宽度掩码  
#define SPI_DATA_8BIT      0x0000   // 8位宽度 
#define SPI_DATA_16BIT     0x0100   // 16位宽度 
#define SPI_DATA_24BIT     0x0200   // 24位宽度 
#define SPI_DATA_32BIT     0x0300   // 32位宽度 
#define SPI_DATA_48BIT     0x0400   // 48位宽度 
#define SPI_DATA_64BIT     0x0500   // 64位宽度  

/* SPI 句柄结构体 */
struct soft_spi_bus_t{
    // 硬件接口钩子
    void    (*set_sck)(uint8_t level);
    void    (*set_mosi)(uint8_t level);
    void    (*set_cs)(uint8_t level);
    uint8_t (*get_miso)(void);
    void    (*delay_us)(uint32_t us);

    uint32_t half_period_us; // 时钟半周期延时
} ;

/**
 * @brief 初始化spi总线
 * 
 * @param spi SPI 句柄
 * @param flags 配置标志 (Mode)
 */
void spi_bus_init(struct soft_spi_bus_t *spi, uint16_t flags);

/**
 * @brief 全功能模拟 SPI 传输 (支持到 64 位)
 * @param spi      SPI 句柄
 * @param flags    配置标志 (Mode, Endian, Width, CS Polarity)
 * @param tx_data  64位发送数据缓冲区指针
 * @param rx_data  64位接收数据缓冲区指针
 * @param len      传输帧数
 */
void spi_transfer(struct soft_spi_bus_t *spi, uint16_t flags, uint64_t *tx_data, uint64_t *rx_data, uint32_t len);

#endif