/**
 * @file ina226.h
 * @brief Platform-independent INA226 multi-instance driver.
 */

#ifndef INA226_H
#define INA226_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INA226_MANUFACTURER_ID_VALUE 0x5449U
#define INA226_DIE_ID_VALUE          0x2260U

#define INA226_REG_CONFIG        0x00U
#define INA226_REG_SHUNT_VOLTAGE 0x01U
#define INA226_REG_BUS_VOLTAGE   0x02U
#define INA226_REG_POWER         0x03U
#define INA226_REG_CURRENT       0x04U
#define INA226_REG_CALIBRATION   0x05U
#define INA226_REG_MASK_ENABLE   0x06U
#define INA226_REG_ALERT_LIMIT   0x07U
#define INA226_REG_MANUFACTURER  0xFEU
#define INA226_REG_DIE_ID        0xFFU

typedef enum {
    INA226_OK = 0,
    INA226_RET_NULL = -1,
    INA226_RET_IO = -2,
    INA226_RET_PARAM = -3,
    INA226_RET_RANGE = -4,
    INA226_RET_ID = -5,
    INA226_RET_CALIBRATION = -6,
} ina226_ret_t;

typedef enum {
    INA226_ADDR_A0_GND_A1_GND = 0x40,
    INA226_ADDR_A0_VS_A1_GND  = 0x41,
    INA226_ADDR_A0_SDA_A1_GND = 0x42,
    INA226_ADDR_A0_SCL_A1_GND = 0x43,
    INA226_ADDR_A0_GND_A1_VS  = 0x44,
    INA226_ADDR_A0_VS_A1_VS   = 0x45,
    INA226_ADDR_A0_SDA_A1_VS  = 0x46,
    INA226_ADDR_A0_SCL_A1_VS  = 0x47,
    INA226_ADDR_A0_GND_A1_SDA = 0x48,
    INA226_ADDR_A0_VS_A1_SDA  = 0x49,
    INA226_ADDR_A0_SDA_A1_SDA = 0x4A,
    INA226_ADDR_A0_SCL_A1_SDA = 0x4B,
    INA226_ADDR_A0_GND_A1_SCL = 0x4C,
    INA226_ADDR_A0_VS_A1_SCL  = 0x4D,
    INA226_ADDR_A0_SDA_A1_SCL = 0x4E,
    INA226_ADDR_A0_SCL_A1_SCL = 0x4F,
} ina226_address_t;

typedef enum {
    INA226_AVG_1 = 0,
    INA226_AVG_4,
    INA226_AVG_16,
    INA226_AVG_64,
    INA226_AVG_128,
    INA226_AVG_256,
    INA226_AVG_512,
    INA226_AVG_1024,
} ina226_avg_t;

typedef enum {
    INA226_CT_140US = 0,
    INA226_CT_204US,
    INA226_CT_332US,
    INA226_CT_588US,
    INA226_CT_1100US,
    INA226_CT_2116US,
    INA226_CT_4156US,
    INA226_CT_8244US,
} ina226_conv_time_t;

typedef enum {
    INA226_MODE_POWER_DOWN_0 = 0,
    INA226_MODE_SHUNT_TRIG,
    INA226_MODE_BUS_TRIG,
    INA226_MODE_SHUNT_BUS_TRIG,
    INA226_MODE_POWER_DOWN_1,
    INA226_MODE_SHUNT_CONT,
    INA226_MODE_BUS_CONT,
    INA226_MODE_SHUNT_BUS_CONT,
} ina226_mode_t;

typedef enum {
    INA226_ALERT_NONE = 0x0000,
    INA226_ALERT_SHUNT_OVERVOLTAGE = 0x8000,
    INA226_ALERT_SHUNT_UNDERVOLTAGE = 0x4000,
    INA226_ALERT_BUS_OVERVOLTAGE = 0x2000,
    INA226_ALERT_BUS_UNDERVOLTAGE = 0x1000,
    INA226_ALERT_POWER_OVER_LIMIT = 0x0800,
    INA226_ALERT_CONVERSION_READY = 0x0400,
} ina226_alert_function_t;

typedef enum {
    INA226_ALERT_POLARITY_ACTIVE_LOW = 0,
    INA226_ALERT_POLARITY_ACTIVE_HIGH = 1,
} ina226_alert_polarity_t;

typedef enum {
    INA226_ALERT_TRANSPARENT = 0,
    INA226_ALERT_LATCHED = 1,
} ina226_alert_latch_t;

typedef int (*ina226_read_reg16_t)(void *user,
                                   uint8_t address,
                                   uint8_t reg,
                                   uint16_t *value);
typedef int (*ina226_write_reg16_t)(void *user,
                                    uint8_t address,
                                    uint8_t reg,
                                    uint16_t value);

typedef struct {
    ina226_read_reg16_t read_reg16;
    ina226_write_reg16_t write_reg16;
} ina226_i2c_ops_t;

typedef struct {
    const ina226_i2c_ops_t *ops;
    void *user;
    uint8_t address;
    float current_lsb_A;
    float power_lsb_W;
    uint16_t calibration_value;
    uint16_t config_value;
    uint8_t initialized;
    uint8_t calibrated;
} ina226_t;

typedef struct {
    uint16_t manufacturer_id;
    uint16_t die_id;
    uint16_t device_id;
    uint8_t revision_id;
} ina226_device_info_t;

typedef struct {
    uint16_t calibration_value;
    float current_lsb_A;
    float power_lsb_W;
    float r_shunt_ohm;
    float max_expected_current_A;
} ina226_calibration_t;

typedef struct {
    int16_t shunt_raw;
    uint16_t bus_raw;
    int16_t current_raw;
    uint16_t power_raw;
    uint16_t mask_enable_raw;
    float shunt_voltage_V;
    float bus_voltage_V;
    float current_A;
    float power_W;
    uint8_t conversion_ready;
    uint8_t math_overflow;
    uint8_t alert_function_flag;
} ina226_meas_result_t;

typedef struct {
    uint16_t raw;
    uint8_t alert_function_flag;
    uint8_t conversion_ready;
    uint8_t math_overflow;
    uint8_t active_high;
    uint8_t latched;
} ina226_alert_status_t;

ina226_ret_t ina226_init(ina226_t *dev,
                         const ina226_i2c_ops_t *ops,
                         void *user,
                         uint8_t address);
ina226_ret_t ina226_reset(ina226_t *dev);
ina226_ret_t ina226_read_device_info(ina226_t *dev,
                                     ina226_device_info_t *info);
ina226_ret_t ina226_configure(ina226_t *dev,
                              ina226_avg_t avg,
                              ina226_conv_time_t bus_ct,
                              ina226_conv_time_t shunt_ct,
                              ina226_mode_t mode);
ina226_ret_t ina226_calibrate(ina226_t *dev,
                              float r_shunt_ohm,
                              float max_expected_current_A,
                              float current_lsb_A,
                              ina226_calibration_t *cal_out);
ina226_ret_t ina226_read_bus_voltage(ina226_t *dev,
                                     uint16_t *raw,
                                     float *voltage_V);
ina226_ret_t ina226_read_shunt_voltage(ina226_t *dev,
                                       int16_t *raw,
                                       float *voltage_V);
ina226_ret_t ina226_read_current(ina226_t *dev,
                                 int16_t *raw,
                                 float *current_A);
ina226_ret_t ina226_read_power(ina226_t *dev,
                               uint16_t *raw,
                               float *power_W);
ina226_ret_t ina226_read_all(ina226_t *dev, ina226_meas_result_t *meas);
ina226_ret_t ina226_configure_alert(ina226_t *dev,
                                    ina226_alert_function_t function,
                                    uint16_t limit_raw,
                                    ina226_alert_polarity_t polarity,
                                    ina226_alert_latch_t latch);
ina226_ret_t ina226_read_alert(ina226_t *dev,
                               ina226_alert_status_t *status);
ina226_ret_t ina226_dump_registers(ina226_t *dev,
                                   uint16_t regs[8]);

#ifdef __cplusplus
}
#endif

#endif /* INA226_H */
