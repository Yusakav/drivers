#ifndef USBPD_H
#define USBPD_H

#include "pd_sink.h"
#include "pd_source.h"
#include "usbpd_message.h"

#define USBPD_ROLE_INACTIVE 0xffU
#define USBPD_DRP_TOGGLE_DEFAULT_MS 75U

typedef struct usbpd_config_t
{
    uint8_t port_type;
    uint8_t preferred_role;
    uint16_t drp_toggle_ms;
    struct pd_sink_config_t sink;
    struct pd_source_config_t source;
} usbpd_config_t;

typedef struct usbpd_status_t
{
    uint8_t port_type;
    uint8_t power_role;
    uint8_t attached;
    uint8_t reserved;
    union {
        struct pd_sink_status_t sink;
        struct pd_source_status_t source;
    } policy;
} usbpd_status_t;

int usbpd_request(uint8_t port, uint32_t voltage_mv, uint32_t current_ma);
int usbpd_get_status(uint8_t port, usbpd_status_t *status);
int usbpd_send_control(uint8_t port, uint8_t sop, uint8_t type);
int usbpd_send_data_objects(uint8_t port, uint8_t sop, uint8_t type, const uint32_t *objects, uint8_t count);
int usbpd_send_extended(uint8_t port, uint8_t sop, uint8_t type, const uint8_t *data, uint16_t length);

int usbpd_init(uint8_t port, const usbpd_config_t *config);
void usbpd_task(uint8_t port, uint32_t now_ms);
#endif
