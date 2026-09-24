/**
 * @file ina226_app.h
 * @brief Platform-independent INA226 monitoring state-machine example.
 */

#ifndef INA226_APP_H
#define INA226_APP_H

#include <stdint.h>
#include "ina226.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INA226_APP_STATE_INIT = 0,
    INA226_APP_STATE_CHECK_ID,
    INA226_APP_STATE_CONFIGURE,
    INA226_APP_STATE_MONITOR,
    INA226_APP_STATE_CHECK_ALERT,
    INA226_APP_STATE_DUMP,
    INA226_APP_STATE_IDLE,
    INA226_APP_STATE_ERROR,
} ina226_app_state_t;

typedef uint32_t (*ina226_app_get_ms_t)(void *user);
typedef void (*ina226_app_notify_t)(ina226_app_state_t state,
                                    const char *message,
                                    void *user);

typedef struct {
    const ina226_i2c_ops_t *ops;
    void *ops_user;
    uint8_t address;

    float r_shunt_ohm;
    float max_expected_current_A;
    float current_lsb_A;

    ina226_avg_t avg;
    ina226_conv_time_t bus_ct;
    ina226_conv_time_t shunt_ct;
    ina226_mode_t mode;

    float bus_over_voltage_V;
    float bus_under_voltage_V;
    float current_over_A;
    float power_over_W;
    uint32_t sample_interval_ms;

    ina226_app_get_ms_t get_ms;
    void *time_user;
    ina226_app_notify_t notify;
    void *notify_user;
} ina226_app_config_t;

typedef struct {
    float vbus_max_V;
    float vbus_min_V;
    float vbus_sum_V;
    float current_max_A;
    float current_min_A;
    float current_sum_A;
    float power_max_W;
    float power_sum_W;
    uint32_t sample_count;
    uint32_t alert_count;
    uint32_t overflow_count;
    uint8_t bus_ov_triggered;
    uint8_t bus_uv_triggered;
    uint8_t current_oc_triggered;
    uint8_t power_ov_triggered;
} ina226_app_stats_t;

typedef struct {
    ina226_t dev;
    ina226_app_config_t cfg;
    ina226_app_state_t state;
    ina226_app_state_t last_state;
    ina226_app_stats_t stats;
    ina226_meas_result_t last_meas;
    ina226_device_info_t device_info;
    ina226_calibration_t calibration;
    uint16_t dump_regs[8];
    uint32_t last_sample_ms;
    uint8_t initialized;
} ina226_app_t;

ina226_ret_t ina226_app_init(ina226_app_t *app,
                             const ina226_app_config_t *cfg);
ina226_ret_t ina226_app_process(ina226_app_t *app);
ina226_app_state_t ina226_app_get_state(const ina226_app_t *app);

int ina226_soft_i2c_read_reg16(void *user,
                               uint8_t address,
                               uint8_t reg,
                               uint16_t *value);
int ina226_soft_i2c_write_reg16(void *user,
                                uint8_t address,
                                uint8_t reg,
                                uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* INA226_APP_H */
