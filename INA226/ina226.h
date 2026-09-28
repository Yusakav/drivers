/**
* @file ina226.h
* @brief Platform-independent INA226 多实例驱动对外接口声明
*
* 所属模块：INA226 电源/电流监测（德州仪器 INA226，I2C 接口，多实例支持）。
* 对应实现：ina226.c；平台 I2C 依赖：ina226_i2c_ops_t 回调（应用层提供）。
*
* INA226 特性：
*   - 总线电压测量：0V ~ 40V，LSB = 1.25mV，16bit；
*   - 分流电压测量：±80mV，LSB = 2.5μV，16bit 有符号；
*   - 电流/功率：需校准（写入 CALIBRATION 寄存器），LSB 由校准值决定；
*   - 均值模式：1/4/16/64/128/256/512/1024 次平均；
*   - 转换时间：140μs ~ 8.244ms（8 档）；
*   - 工作模式：触发/连续 × 总线/分流/两者；
*   - Alert：6 种函数 + 极性 + 锁存；
*   - I2C 地址：0x40 ~ 0x4F（A0/A1 引脚 16 种组合）。
*
* 使用约定：
*   1. 实现 ina226_i2c_ops_t（read_reg16 / write_reg16）；
*   2. ina226_init → 读 Manufacturer/Die ID 校验；
*   3. ina226_calibrate（必须，否则 read_current/power 返回 RET_CALIBRATION）；
*   4. ina226_configure（可选，不调则默认 1 次平均/1.1ms/连续分流+总线）；
*   5. ina226_read_bus_voltage / read_shunt_voltage / read_current / read_power / read_all。
*/
#ifndef INA226_H
#define INA226_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 芯片标识 ===== */
#define INA226_MANUFACTURER_ID_VALUE 0x5449U   /**< Manufacturer ID 寄存器固定值（"TI" ASCII） */
#define INA226_DIE_ID_VALUE          0x2260U   /**< Die ID 寄存器固定值（高 12bit=设备 ID 0x226） */

/* ===== 寄存器地址 ===== */
#define INA226_REG_CONFIG        0x00U   /**< 配置寄存器（均值/转换时间/模式） */
#define INA226_REG_SHUNT_VOLTAGE 0x01U   /**< 分流电压寄存器（16bit 有符号） */
#define INA226_REG_BUS_VOLTAGE   0x02U   /**< 总线电压寄存器（15bit，bit15=预留） */
#define INA226_REG_POWER         0x03U   /**< 功率寄存器（校准后有效） */
#define INA226_REG_CURRENT       0x04U   /**< 电流寄存器（校准后有效，16bit 有符号） */
#define INA226_REG_CALIBRATION   0x05U   /**< 校准寄存器（写入后电流/功率 LSB 确定） */
#define INA226_REG_MASK_ENABLE   0x06U   /**< Alert 掩码/使能寄存器 */
#define INA226_REG_ALERT_LIMIT   0x07U   /**< Alert 阈值寄存器 */
#define INA226_REG_MANUFACTURER  0xFEU   /**< Manufacturer ID 只读寄存器（0x5449） */
#define INA226_REG_DIE_ID        0xFFU   /**< Die ID 只读寄存器（0x226x，低 4bit=revision） */

/* ===== 返回码 ===== */
typedef enum {
    INA226_OK = 0,                    /**< 成功 */
    INA226_RET_NULL = -1,             /**< 参数指针为 NULL */
    INA226_RET_IO = -2,               /**< I2C 读写失败 */
    INA226_RET_PARAM = -3,            /**< 参数超出合法范围 */
    INA226_RET_RANGE = -4,            /**< 校准值超出 [1, 32767] 或地址非法 */
    INA226_RET_ID = -5,               /**< Manufacturer/Die ID 不匹配 */
    INA226_RET_CALIBRATION = -6,      /**< 未校准就读电流/功率 */
} ina226_ret_t;

/* ===== I2C 地址枚举（A0/A1 引脚组合） ===== */
typedef enum {
    INA226_ADDR_A0_GND_A1_GND = 0x40,   /**< A0=GND, A1=GND */
    INA226_ADDR_A0_VS_A1_GND  = 0x41,   /**< A0=VS,  A1=GND */
    INA226_ADDR_A0_SDA_A1_GND = 0x42,   /**< A0=SDA, A1=GND */
    INA226_ADDR_A0_SCL_A1_GND = 0x43,   /**< A0=SCL, A1=GND */
    INA226_ADDR_A0_GND_A1_VS  = 0x44,   /**< A0=GND, A1=VS  */
    INA226_ADDR_A0_VS_A1_VS   = 0x45,   /**< A0=VS,  A1=VS  */
    INA226_ADDR_A0_SDA_A1_VS  = 0x46,   /**< A0=SDA, A1=VS  */
    INA226_ADDR_A0_SCL_A1_VS  = 0x47,   /**< A0=SCL, A1=VS  */
    INA226_ADDR_A0_GND_A1_SDA = 0x48,   /**< A0=GND, A1=SDA */
    INA226_ADDR_A0_VS_A1_SDA  = 0x49,   /**< A0=VS,  A1=SDA */
    INA226_ADDR_A0_SDA_A1_SDA = 0x4A,   /**< A0=SDA, A1=SDA */
    INA226_ADDR_A0_SCL_A1_SDA = 0x4B,   /**< A0=SCL, A1=SDA */
    INA226_ADDR_A0_GND_A1_SCL = 0x4C,   /**< A0=GND, A1=SCL */
    INA226_ADDR_A0_VS_A1_SCL  = 0x4D,   /**< A0=VS,  A1=SCL */
    INA226_ADDR_A0_SDA_A1_SCL = 0x4E,   /**< A0=SDA, A1=SCL */
    INA226_ADDR_A0_SCL_A1_SCL = 0x4F,   /**< A0=SCL, A1=SCL */
} ina226_address_t;

/* ===== 配置枚举 ===== */

/** @brief 均值模式（CONFIG 寄存器 bit[11:9]） */
typedef enum {
    INA226_AVG_1 = 0,     /**< 1 次（默认） */
    INA226_AVG_4,         /**< 4 次 */
    INA226_AVG_16,        /**< 16 次 */
    INA226_AVG_64,        /**< 64 次 */
    INA226_AVG_128,       /**< 128 次 */
    INA226_AVG_256,       /**< 256 次 */
    INA226_AVG_512,       /**< 512 次 */
    INA226_AVG_1024,      /**< 1024 次 */
} ina226_avg_t;

/** @brief 转换时间（CONFIG 寄存器 bit[8:6] 或 bit[5:3]） */
typedef enum {
    INA226_CT_140US = 0,    /**< 140μs */
    INA226_CT_204US,        /**< 204μs */
    INA226_CT_332US,        /**< 332μs */
    INA226_CT_588US,        /**< 588μs */
    INA226_CT_1100US,       /**< 1.1ms（默认） */
    INA226_CT_2116US,       /**< 2.116ms */
    INA226_CT_4156US,       /**< 4.156ms */
    INA226_CT_8244US,       /**< 8.244ms */
} ina226_conv_time_t;

/** @brief 工作模式（CONFIG 寄存器 bit[2:0]） */
typedef enum {
    INA226_MODE_POWER_DOWN_0 = 0,    /**< 掉电（低侧） */
    INA226_MODE_SHUNT_TRIG,          /**< 触发分流 */
    INA226_MODE_BUS_TRIG,            /**< 触发总线 */
    INA226_MODE_SHUNT_BUS_TRIG,      /**< 触发分流+总线 */
    INA226_MODE_POWER_DOWN_1,        /**< 掉电（高侧） */
    INA226_MODE_SHUNT_CONT,          /**< 连续分流 */
    INA226_MODE_BUS_CONT,            /**< 连续总线 */
    INA226_MODE_SHUNT_BUS_CONT,      /**< 连续分流+总线（默认） */
} ina226_mode_t;

/** @brief Alert 函数（MASK_ENABLE 寄存器 bit[15:10]） */
typedef enum {
    INA226_ALERT_NONE = 0x0000,              /**< 禁用 */
    INA226_ALERT_SHUNT_OVERVOLTAGE = 0x8000, /**< 分流过压 */
    INA226_ALERT_SHUNT_UNDERVOLTAGE = 0x4000,/**< 分流欠压 */
    INA226_ALERT_BUS_OVERVOLTAGE = 0x2000,   /**< 总线过压 */
    INA226_ALERT_BUS_UNDERVOLTAGE = 0x1000,  /**< 总线欠压 */
    INA226_ALERT_POWER_OVER_LIMIT = 0x0800,  /**< 功率超限 */
    INA226_ALERT_CONVERSION_READY = 0x0400,  /**< 转换完成 */
} ina226_alert_function_t;

/** @brief Alert 极性（MASK_ENABLE 寄存器 bit[1]） */
typedef enum {
    INA226_ALERT_POLARITY_ACTIVE_LOW = 0,   /**< 低有效（默认） */
    INA226_ALERT_POLARITY_ACTIVE_HIGH = 1,  /**< 高有效 */
} ina226_alert_polarity_t;

/** @brief Alert 锁存（MASK_ENABLE 寄存器 bit[0]） */
typedef enum {
    INA226_ALERT_TRANSPARENT = 0,   /**< 透明模式（超限持续时拉高） */
    INA226_ALERT_LATCHED = 1,       /**< 锁存模式（触发后保持，读 MASK_ENABLE 清除） */
} ina226_alert_latch_t;

/* ===== I2C 回调类型（平台层实现） ===== */

/** @brief 读 16bit 寄存器回调 */
typedef int (*ina226_read_reg16_t)(void *user,
                                   uint8_t address,
                                   uint8_t reg,
                                   uint16_t *value);
/** @brief 写 16bit 寄存器回调 */
typedef int (*ina226_write_reg16_t)(void *user,
                                    uint8_t address,
                                    uint8_t reg,
                                    uint16_t value);

/** @brief I2C 操作函数表（应用层构造，传入 ina226_init） */
typedef struct {
    ina226_read_reg16_t read_reg16;     /**< 必须非 NULL */
    ina226_write_reg16_t write_reg16;   /**< 必须非 NULL */
} ina226_i2c_ops_t;

/* ===== 核心结构体 ===== */

/** @brief INA226 设备实例（多实例支持，每个 I2C 器件一个 dev） */
typedef struct {
    const ina226_i2c_ops_t *ops;   /**< I2C 操作回调表 */
    void *user;                     /**< I2C 回调共用的用户参数 */
    uint8_t address;                /**< I2C 地址（0x40 ~ 0x4F） */
    float current_lsb_A;            /**< 校准后电流 LSB（A/step）—— ina226_calibrate 写入 */
    float power_lsb_W;              /**< 校准后功率 LSB（W/step）= current_lsb × 25 */
    uint16_t calibration_value;     /**< CALIBRATION 寄存器写入值（1 ~ 32767） */
    uint16_t config_value;          /**< CONFIG 寄存器当前值（默认 0x4127） */
    uint8_t initialized;            /**< 初始化完成标志（1=init 成功） */
    uint8_t calibrated;             /**< 校准完成标志（1=calibrate 成功，否则 read_current/power 报错） */
} ina226_t;

/* ===== 读数/状态结构体 ===== */

/** @brief 芯片识别信息（Manufacturer + Die ID 拆解） */
typedef struct {
    uint16_t manufacturer_id;   /**< Manufacturer ID 寄存器原始值（应为 0x5449） */
    uint16_t die_id;            /**< Die ID 寄存器原始值（应为 0x226x） */
    uint16_t device_id;         /**< Die ID 高 12bit（0x2260 → 0x226） */
    uint8_t revision_id;        /**< Die ID 低 4bit（芯片版本） */
} ina226_device_info_t;

/** @brief 校准输出（含计算过程，供应用层记录） */
typedef struct {
    uint16_t calibration_value;         /**< 写入 CALIBRATION 寄存器的值 */
    float current_lsb_A;                /**< 电流 LSB（A/step） */
    float power_lsb_W;                  /**< 功率 LSB（W/step） */
    float r_shunt_ohm;                  /**< 分流电阻（Ω） */
    float max_expected_current_A;       /**< 最大期望电流（A） */
} ina226_calibration_t;

/** @brief 完整测量结果（read_all 输出） */
typedef struct {
    int16_t shunt_raw;          /**< 分流电压原始码（有符号） */
    uint16_t bus_raw;            /**< 总线电压原始码（bit15=0） */
    int16_t current_raw;        /**< 电流原始码（有符号，校准后有效） */
    uint16_t power_raw;         /**< 功率原始码（校准后有效） */
    uint16_t mask_enable_raw;   /**< MASK_ENABLE 寄存器原始值 */
    float shunt_voltage_V;      /**< 分流电压（V）= shunt_raw × 2.5μV */
    float bus_voltage_V;        /**< 总线电压（V）= bus_raw × 1.25mV */
    float current_A;            /**< 电流（A）= current_raw × current_lsb */
    float power_W;              /**< 功率（W）= power_raw × power_lsb */
    uint8_t conversion_ready;   /**< MASK_ENABLE bit3=CVRF（转换完成标志） */
    uint8_t math_overflow;      /**< MASK_ENABLE bit2=OVF（计算溢出标志） */
    uint8_t alert_function_flag;/**< MASK_ENABLE bit4=AFF（Alert 函数标志） */
} ina226_meas_result_t;

/** @brief Alert 状态（read_alert 输出） */
typedef struct {
    uint16_t raw;               /**< MASK_ENABLE 寄存器原始值 */
    uint8_t alert_function_flag;/**< bit4=AFF */
    uint8_t conversion_ready;   /**< bit3=CVRF */
    uint8_t math_overflow;      /**< bit2=OVF */
    uint8_t active_high;        /**< bit1=APOL（1=高有效） */
    uint8_t latched;            /**< bit0=LEN（1=锁存模式） */
} ina226_alert_status_t;

/* ===== 对外接口 ===== */

/**
 * @brief  初始化 INA226 设备
 *
 * 流程：memset 清零 → 绑定 ops/user/address → 读 Manufacturer/Die ID 校验 →
 *       设置默认 config_value=0x4127。ID 不匹配或 I2C 失败时 memset 回滚。
 *
 * @param dev  输出设备实例（调用者分配）
 * @param ops  I2C 操作回调表（read_reg16/write_reg16 必须非 NULL）
 * @param user  I2C 回调共用参数（可为 NULL）
 * @param address  I2C 地址（0x40 ~ 0x4F，ina226_address_t 枚举值）
 * @return INA226_OK / RET_NULL / RET_RANGE / RET_IO / RET_ID
 */
ina226_ret_t ina226_init(ina226_t *dev,
                         const ina226_i2c_ops_t *ops,
                         void *user,
                         uint8_t address);

/**
 * @brief  软复位 INA226
 *
 * 写入 CONFIG 寄存器 bit15=RESET 触发复位，然后重置 config_value 与校准标志。
 * 复位后 CALIBRATION 寄存器清零，必须重新调 calibrate。
 *
 * @param dev  已初始化的设备实例
 * @return INA226_OK / RET_IO / RET_PARAM / RET_NULL
 */
ina226_ret_t ina226_reset(ina226_t *dev);

/**
 * @brief  读取芯片识别信息
 * @param dev  已初始化的设备实例
 * @param info  输出识别信息（Manufacturer/Die ID + 拆解字段）
 * @return INA226_OK / RET_NULL / RET_IO / RET_PARAM
 */
ina226_ret_t ina226_read_device_info(ina226_t *dev,
                                     ina226_device_info_t *info);

/**
 * @brief  配置 INA226 工作参数
 *
 * 写入 CONFIG 寄存器：avg→bit[11:9], bus_ct→bit[8:6], shunt_ct→bit[5:3], mode→bit[2:0]。
 * 保留位 bit[14:12] 强制写 010b（手册要求）。
 *
 * @param dev  已初始化的设备实例
 * @param avg  均值模式（INA226_AVG_1 ~ INA226_AVG_1024）
 * @param bus_ct  总线电压转换时间
 * @param shunt_ct  分流电压转换时间
 * @param mode  工作模式（触发/连续 × 分流/总线/两者）
 * @return INA226_OK / RET_PARAM / RET_IO / RET_NULL
 */
ina226_ret_t ina226_configure(ina226_t *dev,
                              ina226_avg_t avg,
                              ina226_conv_time_t bus_ct,
                              ina226_conv_time_t shunt_ct,
                              ina226_mode_t mode);

/**
 * @brief  校准 INA226（写入 CALIBRATION 寄存器，确定电流/功率 LSB）
 *
 * 校准公式：calibration = 0.00512 / (selected_lsb × r_shunt_ohm)
 *   selected_lsb：优先用传入 current_lsb_A（>0），否则 = max_expected_current_A / 32768
 *   calibration 必须在 [1, 32767] 范围内
 *   校准后 current_lsb_A 和 power_lsb_W = selected_lsb × 25 存入 dev
 *
 * @param dev  已初始化的设备实例
 * @param r_shunt_ohm  分流电阻值（Ω，必须 > 0）
 * @param max_expected_current_A  最大期望电流（A，必须 > 0）
 * @param current_lsb_A  期望电流 LSB（A/step，0=自动计算）
 * @param cal_out  可选输出校准结果（可为 NULL）
 * @return INA226_OK / RET_NULL / RET_PARAM / RET_RANGE / RET_IO
 */
ina226_ret_t ina226_calibrate(ina226_t *dev,
                              float r_shunt_ohm,
                              float max_expected_current_A,
                              float current_lsb_A,
                              ina226_calibration_t *cal_out);

/**
 * @brief  读取总线电压
 * @param dev  已初始化的设备实例
 * @param raw  可选输出原始码（可为 NULL）
 * @param voltage_V  可选输出电压（V，可为 NULL）= raw × 1.25mV
 * @return INA226_OK / RET_IO / RET_PARAM / RET_NULL
 */
ina226_ret_t ina226_read_bus_voltage(ina226_t *dev,
                                     uint16_t *raw,
                                     float *voltage_V);

/**
 * @brief  读取分流电压
 * @param dev  已初始化的设备实例
 * @param raw  可选输出原始码（int16_t 有符号，可为 NULL）
 * @param voltage_V  可选输出电压（V，可为 NULL）= raw × 2.5μV
 * @return INA226_OK / RET_IO / RET_PARAM / RET_NULL
 */
ina226_ret_t ina226_read_shunt_voltage(ina226_t *dev,
                                       int16_t *raw,
                                       float *voltage_V);

/**
 * @brief  读取电流（必须先 calibrate，否则返回 RET_CALIBRATION）
 * @param dev  已初始化且已校准的设备实例
 * @param raw  可选输出原始码（int16_t 有符号，可为 NULL）
 * @param current_A  可选输出电流（A，可为 NULL）= raw × current_lsb
 * @return INA226_OK / RET_CALIBRATION / RET_IO / RET_PARAM / RET_NULL
 */
ina226_ret_t ina226_read_current(ina226_t *dev,
                                 int16_t *raw,
                                 float *current_A);

/**
 * @brief  读取功率（必须先 calibrate，否则返回 RET_CALIBRATION）
 * @param dev  已初始化且已校准的设备实例
 * @param raw  可选输出原始码（可为 NULL）
 * @param power_W  可选输出功率（W，可为 NULL）= raw × power_lsb
 * @return INA226_OK / RET_CALIBRATION / RET_IO / RET_PARAM / RET_NULL
 */
ina226_ret_t ina226_read_power(ina226_t *dev,
                               uint16_t *raw,
                               float *power_W);

/**
 * @brief  读取完整测量结果（分流电压 + 总线电压 + 电流 + 功率 + Alert 标志）
 *
 * 内部依次调 read_shunt_voltage / read_bus_voltage / read_current / read_power + 读 MASK_ENABLE。
 * 任何一步失败立即返回对应错误码，meas 中已写入的字段可能不完整。
 *
 * @param dev  已初始化且已校准的设备实例
 * @param meas  输出测量结果（含原始码和物理量）
 * @return INA226_OK / RET_CALIBRATION / RET_IO / RET_PARAM / RET_NULL
 */
ina226_ret_t ina226_read_all(ina226_t *dev, ina226_meas_result_t *meas);

/**
 * @brief  配置 Alert 功能
 *
 * 写入 ALERT_LIMIT（阈值）+ MASK_ENABLE（函数/极性/锁存）。
 * function 可多值 OR 组合（如 SHUNT_OVERVOLTAGE | CONVERSION_READY）。
 *
 * @param dev  已初始化的设备实例
 * @param function  Alert 函数（一个或多个 ina226_alert_function_t 枚举值 OR）
 * @param limit_raw  Alert 阈值原始码（含义取决于 function：电压=有符号码/功率=无符号码）
 * @param polarity  极性（低有效/高有效）
 * @param latch  锁存（透明/锁存）
 * @return INA226_OK / RET_PARAM / RET_IO / RET_NULL
 */
ina226_ret_t ina226_configure_alert(ina226_t *dev,
                                    ina226_alert_function_t function,
                                    uint16_t limit_raw,
                                    ina226_alert_polarity_t polarity,
                                    ina226_alert_latch_t latch);

/**
 * @brief  读取 Alert 状态
 * @param dev  已初始化的设备实例
 * @param status  输出 Alert 状态（含 MASK_ENABLE 原始值与 5 个标志位拆解）
 * @return INA226_OK / RET_IO / RET_PARAM / RET_NULL
 */
ina226_ret_t ina226_read_alert(ina226_t *dev,
                               ina226_alert_status_t *status);

/**
 * @brief  转储全部 8 个寄存器（CONFIG ~ ALERT_LIMIT）
 * @param dev  已初始化的设备实例
 * @param regs  输出数组 regs[0..7] 对应寄存器 0x00..0x07
 * @return INA226_OK / RET_IO / RET_PARAM / RET_NULL
 */
ina226_ret_t ina226_dump_registers(ina226_t *dev,
                                   uint16_t regs[8]);

#ifdef __cplusplus
}
#endif

#endif /* INA226_H */
