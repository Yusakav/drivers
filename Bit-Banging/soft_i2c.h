/**
 * @file soft_i2c.h
 * @brief Platform-independent, open-drain software I2C master.
 */
#ifndef SOFT_I2C_H
#define SOFT_I2C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOFT_I2C_DEFAULT_TIMEOUT_US 25000UL

typedef enum {
    SOFT_I2C_OK = 0,
    SOFT_I2C_ERR_NULL = -1,
    SOFT_I2C_ERR_PARAM = -2,
    SOFT_I2C_ERR_BUS_BUSY = -3,
    SOFT_I2C_ERR_ADDR_NACK = -4,
    SOFT_I2C_ERR_DATA_NACK = -5,
    SOFT_I2C_ERR_TIMEOUT = -6,
    SOFT_I2C_ERR_ARBITRATION = -7,
    SOFT_I2C_ERR_PEC = -8,
    SOFT_I2C_ERR_PROTOCOL = -9,
    SOFT_I2C_ERR_BUFFER = -10,
} soft_i2c_ret_t;

typedef void (*soft_i2c_write_line_t)(void *user, uint8_t release_high);
typedef uint8_t (*soft_i2c_read_line_t)(void *user);
typedef void (*soft_i2c_delay_us_t)(void *user, uint32_t us);

typedef struct {
    void *user;
    soft_i2c_write_line_t write_scl;
    soft_i2c_write_line_t write_sda;
    soft_i2c_read_line_t read_scl;
    soft_i2c_read_line_t read_sda;
    soft_i2c_delay_us_t delay_us;
    uint32_t half_period_us;
    uint32_t scl_timeout_us;
} soft_i2c_config_t;

typedef struct {
    soft_i2c_config_t config;
    uint8_t initialized;
} soft_i2c_bus_t;

enum {
    SOFT_I2C_MSG_READ = 0x0001U,
    SOFT_I2C_MSG_TEN_BIT = 0x0002U,
    /** First received byte is a byte count; trailing_bytes are read after it. */
    SOFT_I2C_MSG_RECV_LEN = 0x0004U,
};

typedef struct {
    uint16_t address;
    uint16_t flags;
    uint8_t *buffer;
    size_t length;       /**< Input buffer capacity/transfer length. */
    size_t transferred;  /**< Output number of bytes transferred. */
    uint8_t trailing_bytes;
} soft_i2c_msg_t;

soft_i2c_ret_t soft_i2c_init(soft_i2c_bus_t *bus,
                             const soft_i2c_config_t *config);
soft_i2c_ret_t soft_i2c_transfer(soft_i2c_bus_t *bus,
                                 soft_i2c_msg_t *messages,
                                 size_t count);
soft_i2c_ret_t soft_i2c_probe(soft_i2c_bus_t *bus,
                              uint16_t address,
                              uint8_t ten_bit);
soft_i2c_ret_t soft_i2c_recover(soft_i2c_bus_t *bus);

#ifdef __cplusplus
}
#endif

#endif /* SOFT_I2C_H */
