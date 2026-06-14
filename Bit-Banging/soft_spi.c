/**
 * @file soft_spi.c
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-12-31
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "soft_spi.h"

/**
 * @brief 初始化spi总线
 *
 * @param spi SPI 句柄
 * @param flags 配置标志 (Mode)
 */
void spi_bus_init (struct soft_spi_bus_t *spi, uint16_t flags) {
    uint8_t cpol = (flags & 0x02) ? 1 : 0;
    uint8_t cs_active = (flags & SPI_CS_HIGH) ? 1 : 0;
    spi->set_sck (cpol);
    spi->set_cs (!cs_active);
    spi->set_mosi (0);
    spi->delay_us (spi->half_period_us * 2);
}

/**
 * @brief 全功能模拟 SPI 传输 (支持到 64 位)
 * @param spi      SPI 句柄
 * @param flags    配置标志 (Mode, Endian, Width, CS Polarity)
 * @param tx_data  64位发送数据缓冲区指针
 * @param rx_data  64位接收数据缓冲区指针
 * @param len      传输帧数
 */
void spi_transfer (struct soft_spi_bus_t *spi, uint16_t flags, uint64_t *tx_data, uint64_t *rx_data, uint32_t len) {
    uint8_t cpol = (flags & 0x02) ? 1 : 0;
    uint8_t cpha = (flags & 0x01) ? 1 : 0;
    uint8_t lsb = (flags & SPI_LSB_FIRST) ? 1 : 0;
    uint8_t cs_active = (flags & SPI_CS_HIGH) ? 1 : 0;
    uint8_t bits;

    switch (flags & SPI_DATA_MASK) {
    case SPI_DATA_64BIT: bits = 64; break;
    case SPI_DATA_32BIT: bits = 32; break;
    case SPI_DATA_24BIT: bits = 24; break;
    case SPI_DATA_16BIT: bits = 16; break;
    default: bits = 8; break;
    }

    spi->set_sck (cpol);
    spi->delay_us (spi->half_period_us);

    if (!(flags & SPI_CS_NONE)) {
        spi->set_cs (cs_active);
        spi->delay_us (spi->half_period_us);
    }

    for (uint32_t i = 0; i < len; i++) {
        uint64_t send_val = 0;
        uint64_t recv_val = 0;

        /* --- 核心修复：根据位宽决定指针步进方式 --- */
        if (tx_data) {
            if (bits <= 8)
                send_val = ((uint8_t *)tx_data)[i];
            else if (bits <= 16)
                send_val = ((uint16_t *)tx_data)[i];
            else if (bits <= 32)
                send_val = ((uint32_t *)tx_data)[i];
            else
                send_val = tx_data[i];  // 64位
        }

        for (uint8_t b = 0; b < bits; b++) {
            uint8_t bit_pos = lsb ? b : (bits - 1 - b);

            if (cpha == 0)
                spi->set_mosi ((send_val >> bit_pos) & 0x01);
            spi->delay_us (spi->half_period_us);

            spi->set_sck (!cpol);  // 第一个边沿
            if (cpha == 0) {
                if (spi->get_miso())
                    recv_val |= ((uint64_t)1 << bit_pos);
            } else {
                spi->set_mosi ((send_val >> bit_pos) & 0x01);
            }
            spi->delay_us (spi->half_period_us);

            spi->set_sck (cpol);  // 第二个边沿
            if (cpha == 1) {
                if (spi->get_miso())
                    recv_val |= ((uint64_t)1 << bit_pos);
            }
        }
        if (rx_data) {
            if (bits <= 8)
                ((uint8_t *)rx_data)[i] = (uint8_t)recv_val;
            else if (bits <= 16)
                ((uint16_t *)rx_data)[i] = (uint16_t)recv_val;
            else if (bits <= 32)
                ((uint32_t *)rx_data)[i] = (uint32_t)recv_val;
            else
                rx_data[i] = recv_val;
        }
    }
    if (!(flags & SPI_CS_NONE)) {
        spi->delay_us(spi->half_period_us);
        spi->set_cs(!cs_active);
    }
}