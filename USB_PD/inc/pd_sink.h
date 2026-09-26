#ifndef PD_SINK_H
#define PD_SINK_H

#include "usbpd_types.h"

enum pd_sink_state_e {
    PD_SINK_UNATTACHED = 0U,
    PD_SINK_ATTACH_WAIT,
    PD_SINK_DISCOVERY,
    PD_SINK_CABLE_DISCOVERY,
    PD_SINK_EPR_CAPS,
    PD_SINK_EPR_ENTER,
    PD_SINK_REQUESTED,
    PD_SINK_TRANSITION,
    PD_SINK_READY,
    PD_SINK_SOFT_RESET,
    PD_SINK_HARD_RESET,
    PD_SINK_ERROR_RECOVERY,
};

struct pd_sink_features_t {
    uint8_t pps : 1;
    uint8_t epr : 1;
    uint8_t usb_communications : 1;
    uint8_t require_5a_cable : 1;
    uint8_t trace_print : 1;
    uint8_t reserved : 3;
};

struct pd_sink_config_t {
    union usbpd_pdo_u sink_pdo[USBPD_MAX_DATA_OBJ];
    union usbpd_pdo_u epr_sink_pdo[USBPD_MAX_DATA_OBJ];
    uint32_t max_voltage_mv;
    uint32_t max_current_ma;
    uint32_t max_power_mw;
    uint8_t sink_pdo_count;
    uint8_t epr_sink_pdo_count;
    struct pd_sink_features_t features;
};

struct pd_sink_status_t {
    uint32_t requested_voltage_mv;
    uint32_t requested_current_ma;
    uint32_t negotiated_voltage_mv;
    uint32_t negotiated_current_ma;
    uint32_t vbus_mv;
    uint8_t selected_pdo;
    uint8_t source_pdo_count;
    uint8_t state : 4;
    uint8_t contract_valid : 1;
    uint8_t fallback_active : 1;
    uint8_t cable_5a : 1;
    uint8_t epr_active : 1;
};

int pd_sink_init(const struct pd_sink_config_t *config);
void pd_sink_task(uint32_t now_ms);
int pd_sink_request(uint32_t voltage_mv, uint32_t current_ma);
int pd_sink_get_status(struct pd_sink_status_t *status);
void pd_sink_trace_service(uint8_t max_records);
void pd_sink_trace_clear(void);

#endif
