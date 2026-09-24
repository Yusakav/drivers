/**
 * @file ina226_app.c
 * @brief Platform-independent INA226 monitoring state-machine example.
 */

#include "ina226_app.h"
#include "soft_i2c.h"

#include <stddef.h>
#include <string.h>

#define INA226_APP_DEFAULT_SAMPLE_INTERVAL_MS 200U

static void notify_state(ina226_app_t *app, const char *message)
{
    if ((app != NULL) && (app->cfg.notify != NULL)) {
        app->cfg.notify(app->state, message, app->cfg.notify_user);
    }
}

static void set_state(ina226_app_t *app,
                      ina226_app_state_t state,
                      const char *message)
{
    app->last_state = app->state;
    app->state = state;
    notify_state(app, message);
}

static void init_stats(ina226_app_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->vbus_min_V = 1000000.0f;
    stats->current_min_A = 1000000.0f;
}

static void update_stats(ina226_app_stats_t *stats,
                         const ina226_meas_result_t *meas)
{
    if (meas->bus_voltage_V > stats->vbus_max_V) {
        stats->vbus_max_V = meas->bus_voltage_V;
    }
    if (meas->bus_voltage_V < stats->vbus_min_V) {
        stats->vbus_min_V = meas->bus_voltage_V;
    }
    if (meas->current_A > stats->current_max_A) {
        stats->current_max_A = meas->current_A;
    }
    if (meas->current_A < stats->current_min_A) {
        stats->current_min_A = meas->current_A;
    }
    if (meas->power_W > stats->power_max_W) {
        stats->power_max_W = meas->power_W;
    }

    stats->vbus_sum_V += meas->bus_voltage_V;
    stats->current_sum_A += meas->current_A;
    stats->power_sum_W += meas->power_W;
    stats->sample_count++;

    if (meas->math_overflow != 0U) {
        stats->overflow_count++;
    }
}

static uint8_t check_thresholds(ina226_app_t *app)
{
    uint8_t fault = 0U;
    ina226_app_stats_t *stats = &app->stats;
    const ina226_meas_result_t *meas = &app->last_meas;

    if ((app->cfg.bus_over_voltage_V > 0.0f) &&
        (meas->bus_voltage_V > app->cfg.bus_over_voltage_V)) {
        stats->bus_ov_triggered = 1U;
        fault = 1U;
    }
    if ((app->cfg.bus_under_voltage_V > 0.0f) &&
        (meas->bus_voltage_V < app->cfg.bus_under_voltage_V)) {
        stats->bus_uv_triggered = 1U;
        fault = 1U;
    }
    if ((app->cfg.current_over_A > 0.0f) &&
        (meas->current_A > app->cfg.current_over_A)) {
        stats->current_oc_triggered = 1U;
        fault = 1U;
    }
    if ((app->cfg.power_over_W > 0.0f) &&
        (meas->power_W > app->cfg.power_over_W)) {
        stats->power_ov_triggered = 1U;
        fault = 1U;
    }

    if (fault != 0U) {
        stats->alert_count++;
    }
    return fault;
}

static ina226_ret_t process_init(ina226_app_t *app)
{
    ina226_ret_t ret = ina226_init(&app->dev,
                                   app->cfg.ops,
                                   app->cfg.ops_user,
                                   app->cfg.address);
    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "ina226 init failed");
        return ret;
    }

    set_state(app, INA226_APP_STATE_CHECK_ID, "init complete");
    return INA226_OK;
}

static ina226_ret_t process_check_id(ina226_app_t *app)
{
    ina226_ret_t ret = ina226_read_device_info(&app->dev, &app->device_info);

    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "read device info failed");
        return ret;
    }

    set_state(app, INA226_APP_STATE_CONFIGURE, "device id checked");
    return INA226_OK;
}

static ina226_ret_t process_configure(ina226_app_t *app)
{
    ina226_ret_t ret = ina226_configure(&app->dev,
                                        app->cfg.avg,
                                        app->cfg.bus_ct,
                                        app->cfg.shunt_ct,
                                        app->cfg.mode);
    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "configure failed");
        return ret;
    }

    ret = ina226_calibrate(&app->dev,
                           app->cfg.r_shunt_ohm,
                           app->cfg.max_expected_current_A,
                           app->cfg.current_lsb_A,
                           &app->calibration);
    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "calibration failed");
        return ret;
    }

    init_stats(&app->stats);
    app->last_sample_ms = 0U;
    set_state(app, INA226_APP_STATE_MONITOR, "configuration complete");
    return INA226_OK;
}

static ina226_ret_t process_monitor(ina226_app_t *app)
{
    uint32_t now;
    uint32_t interval = app->cfg.sample_interval_ms;
    ina226_ret_t ret;

    if (app->cfg.get_ms == NULL) {
        set_state(app, INA226_APP_STATE_ERROR, "missing time source");
        return INA226_RET_NULL;
    }

    if (interval == 0U) {
        interval = INA226_APP_DEFAULT_SAMPLE_INTERVAL_MS;
    }

    now = app->cfg.get_ms(app->cfg.time_user);
    if ((app->last_sample_ms != 0U) &&
        ((uint32_t)(now - app->last_sample_ms) < interval)) {
        return INA226_OK;
    }
    app->last_sample_ms = now;

    ret = ina226_read_all(&app->dev, &app->last_meas);
    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "measurement read failed");
        return ret;
    }

    update_stats(&app->stats, &app->last_meas);
    if (check_thresholds(app) != 0U) {
        set_state(app, INA226_APP_STATE_CHECK_ALERT, "threshold triggered");
    }

    return INA226_OK;
}

static ina226_ret_t process_check_alert(ina226_app_t *app)
{
    ina226_ret_t ret = ina226_configure_alert(&app->dev,
                                              INA226_ALERT_NONE,
                                              0U,
                                              INA226_ALERT_POLARITY_ACTIVE_LOW,
                                              INA226_ALERT_TRANSPARENT);
    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "alert clear failed");
        return ret;
    }

    set_state(app, INA226_APP_STATE_DUMP, "alert handled");
    return INA226_OK;
}

static ina226_ret_t process_dump(ina226_app_t *app)
{
    ina226_ret_t ret = ina226_dump_registers(&app->dev, app->dump_regs);

    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "dump failed");
        return ret;
    }

    set_state(app, INA226_APP_STATE_IDLE, "dump complete");
    return INA226_OK;
}

ina226_ret_t ina226_app_init(ina226_app_t *app,
                             const ina226_app_config_t *cfg)
{
    if ((app == NULL) || (cfg == NULL)) {
        return INA226_RET_NULL;
    }
    if ((cfg->ops == NULL) || (cfg->ops->read_reg16 == NULL) ||
        (cfg->ops->write_reg16 == NULL) || (cfg->get_ms == NULL)) {
        return INA226_RET_NULL;
    }
    if ((cfg->r_shunt_ohm <= 0.0f) ||
        (cfg->max_expected_current_A <= 0.0f)) {
        return INA226_RET_PARAM;
    }

    memset(app, 0, sizeof(*app));
    app->cfg = *cfg;
    app->state = INA226_APP_STATE_INIT;
    app->last_state = INA226_APP_STATE_IDLE;
    app->initialized = 1U;
    return INA226_OK;
}

ina226_ret_t ina226_app_process(ina226_app_t *app)
{
    if (app == NULL) {
        return INA226_RET_NULL;
    }
    if (app->initialized == 0U) {
        return INA226_RET_PARAM;
    }

    switch (app->state) {
    case INA226_APP_STATE_INIT:
        return process_init(app);
    case INA226_APP_STATE_CHECK_ID:
        return process_check_id(app);
    case INA226_APP_STATE_CONFIGURE:
        return process_configure(app);
    case INA226_APP_STATE_MONITOR:
        return process_monitor(app);
    case INA226_APP_STATE_CHECK_ALERT:
        return process_check_alert(app);
    case INA226_APP_STATE_DUMP:
        return process_dump(app);
    case INA226_APP_STATE_IDLE:
        return INA226_OK;
    case INA226_APP_STATE_ERROR:
    default:
        return INA226_RET_PARAM;
    }
}

ina226_app_state_t ina226_app_get_state(const ina226_app_t *app)
{
    if (app == NULL) {
        return INA226_APP_STATE_ERROR;
    }
    return app->state;
}

int ina226_soft_i2c_read_reg16(void *user,
                               uint8_t address,
                               uint8_t reg,
                               uint16_t *value)
{
    uint8_t data[2];
    int8_t ret;

    if ((user == NULL) || (value == NULL)) {
        return -1;
    }

    ret = i2c_transfer((struct soft_i2c_bus_t *)user,
                       address,
                       (uint16_t)(I2C_M_7BIT | I2C_M_REG_8BIT | I2C_M_RD),
                       reg,
                       data,
                       2U);
    if (ret != I2C_OK) {
        return -1;
    }

    *value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    return 0;
}

int ina226_soft_i2c_write_reg16(void *user,
                                uint8_t address,
                                uint8_t reg,
                                uint16_t value)
{
    uint8_t data[2];
    int8_t ret;

    if (user == NULL) {
        return -1;
    }

    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)(value & 0xFFU);

    ret = i2c_transfer((struct soft_i2c_bus_t *)user,
                       address,
                       (uint16_t)(I2C_M_7BIT | I2C_M_REG_8BIT | I2C_M_WR),
                       reg,
                       data,
                       2U);
    return (ret == I2C_OK) ? 0 : -1;
}
