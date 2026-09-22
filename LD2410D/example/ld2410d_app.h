/**
 * @file ld2410d_app.h
 * @brief Platform-independent LD2410D application state machine example.
 */

#ifndef LD2410D_APP_H
#define LD2410D_APP_H

#include "ld2410d.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LD2410D_APP_STATE_INIT = 0,
    LD2410D_APP_STATE_CHECK,
    LD2410D_APP_STATE_CONFIGURE,
    LD2410D_APP_STATE_OPERATION,
    LD2410D_APP_STATE_IDLE,
    LD2410D_APP_STATE_ERROR,
    LD2410D_APP_STATE_COUNT
} ld2410d_app_state_t;

typedef struct {
    uint32_t enter_ms;
    uint32_t elapsed_ms;
} ld2410d_app_stage_time_t;

typedef struct {
    uint8_t init_ok    : 1;
    uint8_t version_ok : 1;
    uint8_t sn_ok      : 1;
    uint8_t config_ok  : 1;
    uint8_t output_ok  : 1;
    uint8_t detected   : 1;
    uint8_t reserved   : 2;
} ld2410d_app_result_t;

typedef uint32_t (*ld2410d_app_get_ms_t)(void *user);
typedef void (*ld2410d_app_target_cb_t)(void *user,
                                         ld2410d_detect_status_t status,
                                         uint16_t distance);

typedef struct {
    ld2410d_uart_ops_t       uart;
    ld2410d_app_get_ms_t     get_ms;
    void                    *time_user;
    ld2410d_app_target_cb_t  target_cb;
    void                    *target_user;
    uint8_t                  max_distance;
    uint16_t                 disappear_delay;
    uint32_t                 output_mode;
    uint8_t                  save_params;
    uint32_t                 receive_timeout_ms;
    uint32_t                 report_interval_ms;
} ld2410d_app_config_t;

typedef struct {
    ld2410d_app_state_t       state;
    ld2410d_app_state_t       last_state;
    ld2410d_app_result_t      result;
    ld2410d_app_stage_time_t  stages[LD2410D_APP_STATE_COUNT];
    uint8_t                   sub_step;
    uint32_t                  last_report_ms;
    char                      fw_version[32];
    char                      sn_str[32];
    uint8_t                   sn_hex[16];
    ld2410d_detect_status_t   last_status;
    uint16_t                  last_distance;
    ld2410d_ret_t             last_error;
    ld2410d_app_config_t      config;
    ld2410d_t                 dev;
} ld2410d_app_t;

ld2410d_ret_t ld2410d_app_init(ld2410d_app_t *app,
                                const ld2410d_app_config_t *config);
ld2410d_ret_t ld2410d_app_process(ld2410d_app_t *app);
ld2410d_app_state_t ld2410d_app_get_state(const ld2410d_app_t *app);

#ifdef __cplusplus
}
#endif

#endif
