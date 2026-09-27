#ifndef PD_SOURCE_H
#define PD_SOURCE_H

#include "usbpd_def.h"
#include "usbpd_dpm.h"

enum pd_source_state_e
{
    PD_SOURCE_UNATTACHED = 0U,
    PD_SOURCE_ATTACH_WAIT,
    PD_SOURCE_STARTUP,
    PD_SOURCE_SEND_CAPABILITIES,
    PD_SOURCE_WAIT_REQUEST,
    PD_SOURCE_TRANSITION,
    PD_SOURCE_READY,
    PD_SOURCE_SOFT_RESET,
    PD_SOURCE_HARD_RESET,
    PD_SOURCE_ERROR_RECOVERY,
};

struct pd_source_features_t
{
    uint8_t pps : 1;
    uint8_t avs : 1;
    uint8_t usb_communications : 1;
    uint8_t drp : 1;
    uint8_t reserved : 4;
};

struct pd_source_config_t
{
    union usbpd_pdo_u source_pdo[USBPD_MAX_DATA_OBJ];
    union usbpd_pdo_u sink_pdo[USBPD_MAX_DATA_OBJ];
    struct usbpd_source_cap_ext_db_t source_cap_ext;
    struct usbpd_sink_cap_ext_db_t sink_cap_ext;
    union usbpd_source_info_do_u source_info;
    uint8_t source_pdo_count;
    uint8_t sink_pdo_count;
    uint8_t source_cap_ext_valid;
    uint8_t sink_cap_ext_valid;
    uint8_t source_info_valid;
    struct pd_source_features_t features;
    struct usbpd_dpm_callbacks_t dpm;
};

struct pd_source_status_t
{
    uint32_t negotiated_voltage_mv;
    uint32_t negotiated_current_ma;
    uint32_t vbus_mv;
    uint8_t selected_pdo;
    uint8_t state : 4;
    uint8_t contract_valid : 1;
    uint8_t pps_active : 1;
    uint8_t attached : 1;
    uint8_t reserved : 1;
};

uint8_t pd_source_is_attached(void);
int pd_source_get_status(struct pd_source_status_t *status);
int pd_source_send_control(uint8_t sop, uint8_t type);
int pd_source_send_data_objects(uint8_t sop, uint8_t type, const uint32_t *objects, uint8_t count);
int pd_source_send_extended(uint8_t sop, uint8_t type, const uint8_t *data, uint16_t length);

int pd_source_init(const struct pd_source_config_t *config);
int pd_source_policy_init(const struct pd_source_config_t *config, uint8_t initialize_hardware);
void pd_source_task(uint32_t now_ms);
#endif
