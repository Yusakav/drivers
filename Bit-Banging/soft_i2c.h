/**
 * @file soft_i2c.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief 软件模拟 I2C/SMBus 驱动
 * @version 0.1
 * @date 2025-12-31
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef __SOFT_I2C_H
#define __SOFT_I2C_H

#include <stdint.h>

// clang-format off

#define I2C_OK             0     /**< 成功 */
#define I2C_ERR_ADDR_NAK   1     /**< 从机地址阶段无应答 */
#define I2C_ERR_DATA_NAK   2     /**< 数据阶段无应答或 PEC 校验失败 */
#define I2C_ERR_TIMEOUT    3     /**< SCL 被从机长时间拉低 (SMBus Timeout) */
#define I2C_ERR_BUSY       4     /**< 总线被占用或 SDA 被非法拉低 */


/**
 * @brief 功能标志位宏定义
 *
 * 互斥选项组 (每组默认值为 0x0000，不设置即使用默认模式):
 *   - 方向: I2C_M_WR(默认) / I2C_M_RD
 *   - 地址: I2C_M_7BIT(默认) / I2C_M_TEN
 *   - 端序: I2C_M_BIG(默认) / I2C_M_LITTLE
 *   - 寄存器地址宽度: I2C_M_REG_8BIT(默认) / I2C_M_REG_16BIT
 *   - 数据宽度: I2C_M_DATA_8BIT(默认) / I2C_M_DATA_16BIT
 *   - 发送格式: I2C_M_REG_NORMAL(默认) / I2C_M_RAW (直接发送数据，不含寄存器地址)
 *
 * 独立功能开关 (按位或组合使用):
 *   - I2C_M_NOSTOP, I2C_M_NOSTART, I2C_M_IGNORE_NAK, I2C_M_NO_RD_RESTART
 *   - I2C_M_SMBUS_PEC, I2C_M_SMBUS_QUICK, I2C_M_RECV_LEN
 */

/* ---- 互斥选项: 传输方向 (bit 0) ---- */
#define I2C_M_WR            0x0000  /**< [默认] 写操作 */
#define I2C_M_RD            0x0001  /**< 读操作 */

/* ---- 互斥选项: 从机地址模式 (bit 1) ---- */
#define I2C_M_7BIT          0x0000  /**< [默认] 7 位从机地址 */
#define I2C_M_10BIT         0x0002  /**< 10 位从机地址 */

/* ---- 互斥选项: 数据端序 (bit 2) ---- */
#define I2C_M_BIG           0x0000  /**< [默认] 大端模式 (MSB 在先) */
#define I2C_M_LITTLE        0x0004  /**< 小端模式 (数据字节序翻转) */

/* ---- 独立功能开关 (bit 4~7) ---- */
#define I2C_M_NOSTART       0x0010  /**< 跳过起始条件 (START) */
#define I2C_M_NOSTOP        0x0020  /**< 跳过停止条件 (STOP) */
#define I2C_M_IGNORE_NAK    0x0040  /**< 忽略 NAK，继续传输 */
#define I2C_M_NO_RD_RESTART 0x0080  /**< 读操作时不发送重复起始信号 */

/* ---- 互斥选项: 寄存器地址宽度 (bit 8~9) ---- */
#define I2C_M_REG_8BIT      0x0000  /**< [默认] 8 位寄存器地址 */
#define I2C_M_REG_16BIT     0x0200  /**< 16 位寄存器地址 */

/* ---- 互斥选项: 数据宽度 (bit 10) ---- */
#define I2C_M_DATA_8BIT     0x0000  /**< [默认] 8 位数据宽度  */
#define I2C_M_DATA_16BIT    0x0400  /**< 16 位数据宽度 */

/* ---- 互斥选项: 寄存器发送格式 (bit 11) ---- */
#define I2C_M_REG_NORMAL    0x0000  /**< [默认] 正常模式，先发送寄存器地址 */
#define I2C_M_RAW           0x0800  /**< RAW 模式，直接发送/接收数据，不发送寄存器地址 */

/* ---- 独立功能开关 (bit 12~15) ---- */
#define I2C_M_SMBUS_PEC     0x1000  /**< 启用 SMBus PEC 校验 (Packet Error Checking) */
#define I2C_M_SMBUS_QUICK   0x2000  /**< SMBus Quick Command (只发送地址+R/W位，无数据) */
#define I2C_M_RECV_LEN      0x4000  /**< 读第一个字节作为后续要接收的长度 (I2C_SMBUS_BLOCK) */

// clang-format on

/**
 * @brief I2C 总线硬件抽象句柄
 */
struct soft_i2c_bus_t {
    void (*set_scl)(uint8_t level); /**< 设置 SCL 电平 */
    void (*set_sda)(uint8_t level); /**< 设置 SDA 电平 */
    uint8_t (*get_scl)(void);       /**< 读取 SCL 电平 */
    uint8_t (*get_sda)(void);       /**< 读取 SDA 电平 */
    void (*delay_us)(uint32_t us);  /**< 微秒级延时函数 */

    uint32_t half_period_us;        /**< 通讯速率控制 (半周期延时) */
    uint32_t timeout_us;            /**< SCL 超时检测时间 (us)，0 表示使用默认超时 */
};

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
                    uint16_t len);

/**
 * @brief I2C 总线死锁恢复 (发送 9 个时钟脉冲)
 */
void i2c_bus_recover(struct soft_i2c_bus_t *bus);

/**
 * @brief I2C 总线扫描函数
 * @param bus 指向 I2C 总线句柄的指针
 * @param flags 传输标志位组合
 * @return 传输结果状态码
 */
void i2c_scan(struct soft_i2c_bus_t *bus, uint16_t flags);

#endif
