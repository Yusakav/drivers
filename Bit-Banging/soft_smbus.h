/**
 * @file soft_smbus.h
 * @brief SMBus convenience operations built on soft_i2c messages.
 */
#ifndef SOFT_SMBUS_H
#define SOFT_SMBUS_H

#include "soft_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOFT_SMBUS_BLOCK_MAX 32U

typedef enum {
    SOFT_SMBUS_PEC_DISABLED = 0,
    SOFT_SMBUS_PEC_ENABLED = 1,
} soft_smbus_pec_t;

soft_i2c_ret_t soft_smbus_quick(soft_i2c_bus_t *bus,
                                uint8_t address,
                                uint8_t read);
soft_i2c_ret_t soft_smbus_send_byte(soft_i2c_bus_t *bus,
                                    uint8_t address,
                                    uint8_t value,
                                    soft_smbus_pec_t pec);
soft_i2c_ret_t soft_smbus_receive_byte(soft_i2c_bus_t *bus,
                                       uint8_t address,
                                       uint8_t *value,
                                       soft_smbus_pec_t pec);
soft_i2c_ret_t soft_smbus_write_byte_data(soft_i2c_bus_t *bus,
                                          uint8_t address,
                                          uint8_t command,
                                          uint8_t value,
                                          soft_smbus_pec_t pec);
soft_i2c_ret_t soft_smbus_read_byte_data(soft_i2c_bus_t *bus,
                                         uint8_t address,
                                         uint8_t command,
                                         uint8_t *value,
                                         soft_smbus_pec_t pec);
soft_i2c_ret_t soft_smbus_write_word_data(soft_i2c_bus_t *bus,
                                          uint8_t address,
                                          uint8_t command,
                                          uint16_t value,
                                          soft_smbus_pec_t pec);
soft_i2c_ret_t soft_smbus_read_word_data(soft_i2c_bus_t *bus,
                                         uint8_t address,
                                         uint8_t command,
                                         uint16_t *value,
                                         soft_smbus_pec_t pec);
soft_i2c_ret_t soft_smbus_write_block_data(soft_i2c_bus_t *bus,
                                           uint8_t address,
                                           uint8_t command,
                                           const uint8_t *data,
                                           size_t length,
                                           soft_smbus_pec_t pec);
soft_i2c_ret_t soft_smbus_read_block_data(soft_i2c_bus_t *bus,
                                          uint8_t address,
                                          uint8_t command,
                                          uint8_t *data,
                                          size_t capacity,
                                          size_t *length,
                                          soft_smbus_pec_t pec);

#ifdef __cplusplus
}
#endif

#endif /* SOFT_SMBUS_H */
