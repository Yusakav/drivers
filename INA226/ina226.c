/**
 * @file ina226.c
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2026-01-12
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#include "ina226.h"



/**
 * @brief 芯片寄存器定义
 */
#define INA226_REG_CONF                 0x00        /**< 配置寄存器 */
#define INA226_REG_SHUNT_VOLTAGE        0x01        /**< 分流电压寄存器 */
#define INA226_REG_BUS_VOLTAGE          0x02        /**< 总线电压寄存器 */
#define INA226_REG_POWER                0x03        /**< 功率寄存器 */
#define INA226_REG_CURRENT              0x04        /**< 电流寄存器 */
#define INA226_REG_CALIBRATION          0x05        /**< 校准寄存器 */
#define INA226_REG_MASK                 0x06        /**< 掩码寄存器 */
#define INA226_REG_ALERT_LIMIT          0x07        /**< 警报限制寄存器 */
#define INA226_REG_MANUFACTURER         0xFE        /**< 制造商ID寄存器 */
#define INA226_REG_DIE                  0xFF        /**< die id 寄存器 */


