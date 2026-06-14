/**
 * @file soft_i2c.c
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief 软件模拟 I2C/SMBus 驱动实现
 * @version 0.1
 * @date 2025-12-31
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "soft_i2c.h"
#include <stdio.h>

/* ---------------- 内部辅助宏 ---------------- */
#define DEFAULT_TIMEOUT_US  25000   /**< 默认 SCL 超时时间 25ms */

/**
 * @brief 延时半个时钟周期
 */
static inline void i2c_delay_half(struct soft_i2c_bus_t *bus)
{
    bus->delay_us(bus->half_period_us);
}


/**
 * @brief 等待 SCL 被释放，带超时检测
 *        若 get_scl 未注册则直接返回成功（跳过时钟拉伸检测）
 * @return 0 成功，非0 超时
 */
static uint8_t i2c_wait_scl(struct soft_i2c_bus_t *bus)
{
    /* get_scl 为空指针时跳过时钟拉伸检测 */
    if (bus->get_scl == NULL) {
        return 0;
    }
    uint32_t timeout = bus->timeout_us ? bus->timeout_us : DEFAULT_TIMEOUT_US;
    while (bus->get_scl() == 0) {
        if (timeout == 0) {
            return 1; /* 超时 */
        }
        bus->delay_us(1);
        if (timeout > 0) {
            timeout--;
        }
    }
    return 0;
}

/**
 * @brief 产生 I2C 起始信号 (START)
 */
static void i2c_start(struct soft_i2c_bus_t *bus)
{
    bus->set_sda(1);
    i2c_delay_half(bus);
    bus->set_scl(1);
    i2c_delay_half(bus);
    bus->set_sda(0);
    i2c_delay_half(bus);
    bus->set_scl(0);
    i2c_delay_half(bus);
}

/**
 * @brief 产生 I2C 停止信号 (STOP)
 */
static void i2c_stop(struct soft_i2c_bus_t *bus)
{
    bus->set_sda(0);
    i2c_delay_half(bus);
    bus->set_scl(1);
    i2c_delay_half(bus);
    bus->set_sda(1);
    i2c_delay_half(bus);
}

/**
 * @brief 发送一个字节，并读取应答位
 * @param bus 总线句柄
 * @param byte 要发送的字节
 * @return 0 收到 ACK，1 收到 NAK
 */
static uint8_t i2c_write_byte(struct soft_i2c_bus_t *bus, uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++) {
        bus->set_sda((byte & 0x80) ? 1 : 0);
        byte <<= 1;
        i2c_delay_half(bus);
        bus->set_scl(1);
        if (i2c_wait_scl(bus)) {
            return 1; /* 时钟拉伸超时当作 NAK */
        }
        i2c_delay_half(bus);
        bus->set_scl(0);
        i2c_delay_half(bus);
    }

    /* 释放 SDA，准备读取 ACK */
    bus->set_sda(1);
    i2c_delay_half(bus);
    bus->set_scl(1);
    if (i2c_wait_scl(bus)) {
        return 1;
    }
    uint8_t ack = bus->get_sda();
    i2c_delay_half(bus);
    bus->set_scl(0);
    i2c_delay_half(bus);
    return ack;
}

/**
 * @brief 读取一个字节，并发送应答/非应答位
 * @param bus 总线句柄
 * @param ack 1 发送 ACK，0 发送 NAK
 * @return 读取到的字节
 */
static uint8_t i2c_read_byte(struct soft_i2c_bus_t *bus, uint8_t ack)
{
    uint8_t byte = 0;
    bus->set_sda(1); /* 释放 SDA */
    for (uint8_t i = 0; i < 8; i++) {
        byte <<= 1;
        i2c_delay_half(bus);
        bus->set_scl(1);
        if (i2c_wait_scl(bus)) {
            /* 超时，直接返回已读取部分 */
            return byte;
        }
        if (bus->get_sda()) {
            byte |= 0x01;
        }
        i2c_delay_half(bus);
        bus->set_scl(0);
        i2c_delay_half(bus);
    }

    bus->set_sda(ack ? 0 : 1);
    i2c_delay_half(bus);
    bus->set_scl(1);
    i2c_delay_half(bus);
    bus->set_scl(0);
    i2c_delay_half(bus);
    return byte;
}

/**
 * @brief 发送 10 位从机地址 (完整流程: START + 头字节 + ACK + 地址低 8 位 + ACK)
 * @param bus 总线句柄
 * @param addr 10 位从机地址
 * @param rd 读取/写入 R/W 位
 * @return 传输结果状态码
 */
static int8_t i2c_send_10bit_addr(struct soft_i2c_bus_t *bus, uint16_t addr, uint8_t rd)
{
    uint8_t header = 0xF0 | ((addr >> 8) & 0x03) | (rd ? 0x01 : 0x00);
    if (i2c_write_byte(bus, header)) {
        return I2C_ERR_ADDR_NAK;
    }
    if (i2c_write_byte(bus, (uint8_t)(addr & 0xFF))) {
        return I2C_ERR_ADDR_NAK;
    }
    return I2C_OK;
}

/**
 * @brief 发送 7 位从机地址 + R/W 位
 * 
 * @param bus 总线句柄
 * @param addr 7 位从机地址
 * @param rd 读取/写入 R/W 位
 * @return 传输结果状态码
 */
static int8_t i2c_send_7bit_addr(struct soft_i2c_bus_t *bus, uint8_t addr, uint8_t rd)
{
    uint8_t byte = (addr << 1) | (rd ? 0x01 : 0x00);
    if (i2c_write_byte(bus, byte)) {
        return I2C_ERR_ADDR_NAK;
    }
    return I2C_OK;
}

/**
 * @brief CRC-8 计算 (SMBus PEC 多项式 0x07)
 * @param crc 当前 CRC 值
 * @param data 要计算 CRC 的字节
 * @return 计算得到的 CRC 值
 */
static uint8_t crc8_pec(uint8_t crc, uint8_t data)
{
    crc ^= data;
    for (uint8_t i = 0; i < 8; i++) {
        if (crc & 0x80) {
            crc = (crc << 1) ^ 0x07;
        } else {
            crc <<= 1;
        }
    }
    return crc;
}

/**
 * @brief 全功能 I2C/SMBus 传输函数
 * @param bus 指向 I2C 总线句柄
 * @param dev_addr 7位或10位从机地址
 * @param flags 传输标志位组合
 * @param reg_addr 寄存器地址 (根据 flags 自动判断 8/16bit)
 * @param data 数据缓冲区 (写操作为输入, 读操作为输出)
 * @param len 传输字节数
 * @return 传输结果状态码
 */
int8_t i2c_transfer(struct soft_i2c_bus_t *bus,
                    uint16_t dev_addr,
                    uint16_t flags,
                    uint32_t reg_addr,
                    void *data,
                    uint16_t len)
{
    if (bus == NULL || bus->set_scl == NULL || bus->set_sda == NULL ||
        bus->get_sda == NULL || bus->delay_us == NULL) {
        return I2C_ERR_BUSY;
    }

    uint8_t is_rd      = (flags & I2C_M_RD)         ? 1 : 0;
    uint8_t is_10bit   = (flags & I2C_M_10BIT)       ? 1 : 0;
    uint8_t is_little  = (flags & I2C_M_LITTLE)      ? 1 : 0;
    uint8_t nostart    = (flags & I2C_M_NOSTART)     ? 1 : 0;
    uint8_t nostop     = (flags & I2C_M_NOSTOP)      ? 1 : 0;
    uint8_t ignore_nak = (flags & I2C_M_IGNORE_NAK)  ? 1 : 0;
    uint8_t no_rd_restart = (flags & I2C_M_NO_RD_RESTART) ? 1 : 0;
    uint8_t reg_16bit  = (flags & I2C_M_REG_16BIT)   ? 1 : 0;
    uint8_t data_16bit = (flags & I2C_M_DATA_16BIT)  ? 1 : 0;
    uint8_t raw_mode   = (flags & I2C_M_RAW)         ? 1 : 0;
    uint8_t smbus_pec  = (flags & I2C_M_SMBUS_PEC)   ? 1 : 0;
    uint8_t smbus_quick = (flags & I2C_M_SMBUS_QUICK) ? 1 : 0;
    uint8_t recv_len   = (flags & I2C_M_RECV_LEN)    ? 1 : 0;

    int8_t status = I2C_OK;
    uint8_t pec = 0;
    uint16_t actual_len = len;

    /* ---- SMBus Quick Command: 仅发送地址+R/W，无数据 ---- */
    if (smbus_quick) {
        if (!nostart) {
            i2c_start(bus);
        }
        if (is_10bit) {
            status = i2c_send_10bit_addr(bus, dev_addr, is_rd);
        } else {
            status = i2c_send_7bit_addr(bus, (uint8_t)(dev_addr & 0x7F), is_rd);
        }
        if (!nostop && status == I2C_OK) {
            i2c_stop(bus);
        }
        return status;
    }

    /* ---- 写阶段 (写寄存器地址 + 写数据) ---- */
    if (!is_rd || !raw_mode) {
        if (!nostart) {
            i2c_start(bus);
        }

        if (is_10bit) {
            status = i2c_send_10bit_addr(bus, dev_addr, 0);
        } else {
            status = i2c_send_7bit_addr(bus, (uint8_t)(dev_addr & 0x7F), 0);
        }
        if (status != I2C_OK) {
            if (!nostop) { i2c_stop(bus); }
            return status;
        }

        if (smbus_pec) {
            if (is_10bit) {
                pec = crc8_pec(pec, 0xF0 | ((dev_addr >> 8) & 0x03));
                pec = crc8_pec(pec, (uint8_t)(dev_addr & 0xFF));
            } else {
                pec = crc8_pec(pec, (uint8_t)((dev_addr & 0x7F) << 1));
            }
        }

        /* 发送寄存器地址 (RAW 模式跳过) */
        if (!raw_mode) {
            if (reg_16bit) {
                uint8_t reg_hi = (uint8_t)((reg_addr >> 8) & 0xFF);
                uint8_t reg_lo = (uint8_t)(reg_addr & 0xFF);
                if (is_little) {
                    uint8_t tmp = reg_hi; reg_hi = reg_lo; reg_lo = tmp;
                }
                if (i2c_write_byte(bus, reg_hi)) {
                    if (!ignore_nak) { status = I2C_ERR_DATA_NAK; goto xfer_exit; }
                }
                if (smbus_pec) { pec = crc8_pec(pec, reg_hi); }
                if (i2c_write_byte(bus, reg_lo)) {
                    if (!ignore_nak) { status = I2C_ERR_DATA_NAK; goto xfer_exit; }
                }
                if (smbus_pec) { pec = crc8_pec(pec, reg_lo); }
            } else {
                uint8_t reg = (uint8_t)(reg_addr & 0xFF);
                if (i2c_write_byte(bus, reg)) {
                    if (!ignore_nak) { status = I2C_ERR_DATA_NAK; goto xfer_exit; }
                }
                if (smbus_pec) { pec = crc8_pec(pec, reg); }
            }
        }

        /* 写数据 */
        if (!is_rd && data != NULL && actual_len > 0) {
            uint8_t *d8 = (uint8_t *)data;   /* 8bit 模式用的字节指针 */
            uint16_t *d16 = (uint16_t *)data; /* 16bit 模式用的字指针 */
            uint16_t i = 0;
            while (i < actual_len) {
                if (data_16bit) {
                    /*
                     * 16bit 数据模式: data 视为 uint16_t 数组
                     * 每次取一个 word, 按 SMBus Word 协议发送: 低字节在前, 高字节在后
                     * len 仍以字节为单位, 即传 2 = 1 个 word
                     */
                    uint16_t word = d16[i / 2];
                    uint8_t lo = (uint8_t)(word & 0xFF);       /* 低字节先发 */
                    uint8_t hi = (uint8_t)((word >> 8) & 0xFF); /* 高字节后发 */
                    if (i2c_write_byte(bus, lo)) {
                        if (!ignore_nak) { status = I2C_ERR_DATA_NAK; goto xfer_exit; }
                    }
                    if (smbus_pec) { pec = crc8_pec(pec, lo); }
                    if (i2c_write_byte(bus, hi)) {
                        if (!ignore_nak) { status = I2C_ERR_DATA_NAK; goto xfer_exit; }
                    }
                    if (smbus_pec) { pec = crc8_pec(pec, hi); }
                    i += 2;
                } else {
                    if (i2c_write_byte(bus, d8[i])) {
                        if (!ignore_nak) { status = I2C_ERR_DATA_NAK; goto xfer_exit; }
                    }
                    if (smbus_pec) { pec = crc8_pec(pec, d8[i]); }
                    i++;
                }
            }

            if (smbus_pec) {
                if (i2c_write_byte(bus, pec)) {
                    if (!ignore_nak) { status = I2C_ERR_DATA_NAK; goto xfer_exit; }
                }
            }
        }
    }

    /* ---- 读阶段 ---- */
    if (is_rd) {
        if (!no_rd_restart && !raw_mode) {
            i2c_start(bus); /* 重复起始 */
        } else if (raw_mode && !nostart) {
            i2c_start(bus);
        }

        if (is_10bit) {
            status = i2c_send_10bit_addr(bus, dev_addr, 1);
        } else {
            status = i2c_send_7bit_addr(bus, (uint8_t)(dev_addr & 0x7F), 1);
        }
        if (status != I2C_OK) {
            if (!nostop) { i2c_stop(bus); }
            return status;
        }

        if (smbus_pec) {
            if (is_10bit) {
                pec = crc8_pec(0, 0xF0 | ((dev_addr >> 8) & 0x03) | 0x01);
                pec = crc8_pec(pec, (uint8_t)(dev_addr & 0xFF));
            } else {
                pec = crc8_pec(0, (uint8_t)(((dev_addr & 0x7F) << 1) | 0x01));
            }
        }

        if (data != NULL && actual_len > 0) {
            uint8_t *d8 = (uint8_t *)data;   /* 8bit 模式用的字节指针 */
            uint16_t *d16 = (uint16_t *)data; /* 16bit 模式用的字指针 */
            if (data_16bit) {
                /*
                 * 16bit 数据模式: data 视为 uint16_t 数组
                 * 按 SMBus Word 协议接收: 低字节在前, 高字节在后
                 * 每次读 2 字节组装成 1 个 word 存入 d16[]
                 */
                for (uint16_t i = 0; i < actual_len; i += 2) {
                    uint8_t lo = i2c_read_byte(bus, 1); /* 读低字节, ACK */
                    if (smbus_pec) { pec = crc8_pec(pec, lo); }
                    uint8_t hi = i2c_read_byte(bus, (i + 2 < actual_len) ? 1 : 0); /* 读高字节 */
                    if (smbus_pec) { pec = crc8_pec(pec, hi); }
                    d16[i / 2] = ((uint16_t)hi << 8) | lo; /* 组装成 word */
                }
            } else {
                for (uint16_t i = 0; i < actual_len; i++) {
                    uint8_t ack = (i < actual_len - 1) ? 1 : 0;
                    uint8_t byte = i2c_read_byte(bus, ack);
                    if (smbus_pec) { pec = crc8_pec(pec, byte); }
                    d8[i] = byte;

                    /* I2C_M_RECV_LEN: 第一个字节为后续接收长度 */
                    if (recv_len && i == 0) {
                        actual_len = byte + 1;
                        if (actual_len > len) {
                            actual_len = len;
                        }
                    }
                }
            }

            if (smbus_pec) {
                uint8_t rx_pec = i2c_read_byte(bus, 0); /* 读取 PEC 字节 */
                if (rx_pec != pec) {
                    status = I2C_ERR_DATA_NAK; /* PEC 错误 */
                }
            }
        }
    }

xfer_exit:
    if (!nostop) {
        i2c_stop(bus);
    }
    return status;
}

/**
 * @brief 总线死锁恢复函数 (发送 9 个时钟脉冲 + STOP)
 * @param bus 总线句柄
 */
void i2c_bus_recover(struct soft_i2c_bus_t *bus)
{
    if (bus == NULL || bus->set_scl == NULL || bus->set_sda == NULL || bus->delay_us == NULL) {
        return;
    }

    bus->set_sda(1);
    for (uint8_t i = 0; i < 9; i++) {
        bus->set_scl(0);
        bus->delay_us(bus->half_period_us);
        bus->set_scl(1);
        bus->delay_us(bus->half_period_us);
    }
    i2c_stop(bus);
}

/**
 * @brief I2C 总线扫描函数
 * @param bus 总线句柄
 * @param flags 传输标志位
 */
void i2c_scan(struct soft_i2c_bus_t *bus, uint16_t flags)
{
    uint8_t found[128] = {0};

    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        int8_t ret = i2c_transfer(bus, addr, flags, 0, NULL, 0);
        if (ret == I2C_OK) {
            found[addr] = 1;
        }
    }

    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    for (uint8_t row = 0; row < 8; row++) {
        printf("%02x: ", row << 4);
        for (uint8_t col = 0; col < 16; col++) {
            uint8_t addr = (row << 4) | col;
            if (addr < 0x03 || addr > 0x77) {
                printf("   ");
            } else if (found[addr]) {
                printf(" %02x", addr);
            } else {
                printf(" --");
            }
        }
        printf("\n");
    }
}
