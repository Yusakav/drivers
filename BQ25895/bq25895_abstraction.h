/**
 * @file    bq25895_abstraction.h
 * @brief   BQ25895 硬件抽象层 (HAL) - 平台无关 I2C 接口定义
 * @details 定义与具体 MCU 平台无关的 I2C 读写回调函数指针。
 *          用户需根据硬件平台实现这些函数并注册到驱动中。
 * @version 1.0.0
 * @date    2026-06-13
 */

#ifndef __BQ25895_ABSTRACTION_H__
#define __BQ25895_ABSTRACTION_H__

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * INCLUDES
 *===========================================================================*/
#include <stdint.h>

/*===========================================================================
 * TYPE DEFINITIONS - I2C CALLBACKS
 *===========================================================================*/

/** @brief I2C 写操作回调: 向指定寄存器写入单字节 */
typedef int32_t (*bq25895_i2c_write_reg_t)(uint8_t dev_addr,
                                            uint8_t reg_addr,
                                            uint8_t data);

/** @brief I2C 读操作回调: 从指定寄存器读取单字节 */
typedef int32_t (*bq25895_i2c_read_reg_t)(uint8_t dev_addr,
                                           uint8_t reg_addr,
                                           uint8_t *data);

/** @brief 微秒级延时回调 */
typedef void (*bq25895_delay_us_t)(uint32_t us);

/*===========================================================================
 * OPS STRUCTURE
 *===========================================================================*/

/** @brief BQ25895 硬件抽象层操作集 */
typedef struct {
    bq25895_i2c_write_reg_t   i2c_write;    /**< I2C 单字节写 */
    bq25895_i2c_read_reg_t    i2c_read;     /**< I2C 单字节读 */
    bq25895_delay_us_t        delay_us;     /**< 微秒延时 */
} bq25895_hal_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* __BQ25895_ABSTRACTION_H__ */
