/**
 * @file bq25710_soft_i2c_adapter.h
 * @brief Bind the global BQ25710 driver callbacks to one soft-I2C bus.
 */
#ifndef BQ25710_SOFT_I2C_ADAPTER_H
#define BQ25710_SOFT_I2C_ADAPTER_H

#include "bq25710.h"
#include "soft_i2c.h"
#include "soft_smbus.h"

#ifdef __cplusplus
extern "C" {
#endif

bq25710_err_t bq25710_soft_i2c_init(soft_i2c_bus_t *bus,
                                     soft_smbus_pec_t pec);

#ifdef __cplusplus
}
#endif

#endif /* BQ25710_SOFT_I2C_ADAPTER_H */
