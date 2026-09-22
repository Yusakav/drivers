/**
 * @file ld2410d_app.c
 * @brief Platform-independent LD2410D application state machine example.
 */

#include "ld2410d_app.h"
#include <string.h>

#define LD2410D_APP_DEFAULT_RX_TIMEOUT_MS      200U
#define LD2410D_APP_DEFAULT_REPORT_INTERVAL_MS 10000U

static uint32_t app_now_ms(const ld2410d_app_t *app)
{
    return app->config.get_ms(app->config.time_user);
}

static void app_set_state(ld2410d_app_t *app, ld2410d_app_state_t next)
{
    uint32_t now = app_now_ms(app);
    ld2410d_app_state_t old = app->state;

    if (old == next) {
        return;
    }
    if (old < LD2410D_APP_STATE_COUNT) {
        app->stages[old].elapsed_ms = now - app->stages[old].enter_ms;
    }

    app->last_state = old;
    app->state = next;
    app->sub_step = 0U;

    if (next < LD2410D_APP_STATE_COUNT) {
        app->stages[next].enter_ms = now;
        app->stages[next].elapsed_ms = 0U;
    }
}

static ld2410d_ret_t app_fail(ld2410d_app_t *app, ld2410d_ret_t ret)
{
    app->last_error = ret;
    app_set_state(app, LD2410D_APP_STATE_ERROR);
    return ret;
}

static void app_report_target(ld2410d_app_t *app,
                              ld2410d_detect_status_t status,
                              uint16_t distance)
{
    if (status == app->last_status && distance == app->last_distance) {
        return;
    }

    app->last_status = status;
    app->last_distance = distance;
    if (status != LD2410D_DETECT_NONE) {
        app->result.detected = 1U;
    }
    if (app->config.target_cb != NULL) {
        app->config.target_cb(app->config.target_user, status, distance);
    }
}

static ld2410d_ret_t app_process_check(ld2410d_app_t *app)
{
    ld2410d_ret_t ret;

    app->sub_step++;
    switch (app->sub_step) {
    case 1U:
        ret = ld2410d_enable_config(&app->dev);
        if (ret != LD2410D_OK) {
            return app_fail(app, ret);
        }
        break;

    case 2U: {
        uint8_t version_len = 0U;
        ret = ld2410d_read_firmware_version(&app->dev, app->fw_version,
                                             (uint8_t)sizeof(app->fw_version),
                                             &version_len);
        if (ret == LD2410D_OK) {
            app->result.version_ok = 1U;
        }
        break;
    }

    case 3U: {
        uint8_t sn_len = 0U;
        ret = ld2410d_read_sn_char(&app->dev, app->sn_str,
                                   (uint8_t)sizeof(app->sn_str), &sn_len);
        if (ret == LD2410D_OK) {
            app->result.sn_ok = 1U;
        }
        break;
    }

    case 4U:
        ret = ld2410d_end_config(&app->dev);
        if (ret != LD2410D_OK) {
            return app_fail(app, ret);
        }
        app_set_state(app, LD2410D_APP_STATE_CONFIGURE);
        break;

    default:
        break;
    }

    return LD2410D_OK;
}

static ld2410d_ret_t app_process_configure(ld2410d_app_t *app)
{
    ld2410d_ret_t ret;

    app->sub_step++;
    switch (app->sub_step) {
    case 1U:
        ret = ld2410d_enable_config(&app->dev);
        if (ret != LD2410D_OK) {
            return app_fail(app, ret);
        }
        break;

    case 2U:
        app->dev.basic_cfg.max_distance = app->config.max_distance;
        app->dev.basic_cfg.disappear_delay = app->config.disappear_delay;
        ret = ld2410d_write_basic_config(&app->dev);
        if (ret != LD2410D_OK) {
            return app_fail(app, ret);
        }
        break;

    case 3U:
        ret = ld2410d_set_output_mode(&app->dev, app->config.output_mode);
        if (ret != LD2410D_OK) {
            return app_fail(app, ret);
        }
        app->result.output_ok = 1U;
        break;

    case 4U:
        if (app->config.save_params != 0U) {
            ret = ld2410d_save_params(&app->dev);
            if (ret != LD2410D_OK) {
                return app_fail(app, ret);
            }
        }
        break;

    case 5U:
        ret = ld2410d_end_config(&app->dev);
        if (ret != LD2410D_OK) {
            return app_fail(app, ret);
        }
        app->result.config_ok = 1U;
        app_set_state(app, LD2410D_APP_STATE_OPERATION);
        break;

    default:
        break;
    }

    return LD2410D_OK;
}

static ld2410d_ret_t app_process_operation(ld2410d_app_t *app)
{
    ld2410d_engineering_data_t data;
    uint32_t now = app_now_ms(app);
    ld2410d_ret_t ret;

    ret = ld2410d_recv_engineering_frame(&app->dev, &data,
                                         app->config.receive_timeout_ms);
    if (ret == LD2410D_ERR_TIMEOUT) {
        return LD2410D_OK;
    }
    if (ret != LD2410D_OK) {
        return app_fail(app, ret);
    }

    app_report_target(app, data.status, data.distance);
    if ((now - app->last_report_ms) >= app->config.report_interval_ms) {
        app->last_report_ms = now;
    }

    return LD2410D_OK;
}

ld2410d_ret_t ld2410d_app_init(ld2410d_app_t *app,
                                const ld2410d_app_config_t *config)
{
    ld2410d_ret_t ret;

    if (app == NULL || config == NULL) {
        return LD2410D_ERR_NULL;
    }
    if (config->get_ms == NULL || config->uart.send == NULL ||
        config->uart.recv == NULL || config->uart.flush == NULL) {
        return LD2410D_ERR_NULL;
    }
    if (config->max_distance < 7U || config->max_distance > 100U) {
        return LD2410D_ERR_PARAM;
    }
    if (config->output_mode != LD2410D_OUTPUT_MODE_ENGINEERING &&
        config->output_mode != LD2410D_OUTPUT_MODE_NORMAL) {
        return LD2410D_ERR_PARAM;
    }

    memset(app, 0, sizeof(*app));
    app->config = *config;
    if (app->config.receive_timeout_ms == 0U) {
        app->config.receive_timeout_ms = LD2410D_APP_DEFAULT_RX_TIMEOUT_MS;
    }
    if (app->config.report_interval_ms == 0U) {
        app->config.report_interval_ms = LD2410D_APP_DEFAULT_REPORT_INTERVAL_MS;
    }

    ret = ld2410d_init(&app->dev, &app->config.uart);
    if (ret != LD2410D_OK) {
        return ret;
    }

    app->state = LD2410D_APP_STATE_INIT;
    app->last_state = LD2410D_APP_STATE_INIT;
    app->last_status = LD2410D_DETECT_NONE;
    app->stages[LD2410D_APP_STATE_INIT].enter_ms = app_now_ms(app);
    app->result.init_ok = 1U;
    return LD2410D_OK;
}

ld2410d_ret_t ld2410d_app_process(ld2410d_app_t *app)
{
    if (app == NULL) {
        return LD2410D_ERR_NULL;
    }
    if (app->config.get_ms == NULL) {
        return app_fail(app, LD2410D_ERR_NULL);
    }

    switch (app->state) {
    case LD2410D_APP_STATE_INIT:
        app_set_state(app, LD2410D_APP_STATE_CHECK);
        return LD2410D_OK;

    case LD2410D_APP_STATE_CHECK:
        return app_process_check(app);

    case LD2410D_APP_STATE_CONFIGURE:
        return app_process_configure(app);

    case LD2410D_APP_STATE_OPERATION:
        return app_process_operation(app);

    case LD2410D_APP_STATE_IDLE:
    case LD2410D_APP_STATE_ERROR:
    default:
        break;
    }

    return LD2410D_OK;
}

ld2410d_app_state_t ld2410d_app_get_state(const ld2410d_app_t *app)
{
    if (app == NULL) {
        return LD2410D_APP_STATE_ERROR;
    }
    return app->state;
}
