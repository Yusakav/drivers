#ifndef USBPD_DPM_H
#define USBPD_DPM_H

#include "usbpd_def.h"

enum usbpd_dpm_message_category_e
{
    USBPD_DPM_CONTROL_MESSAGE = 0U,
    USBPD_DPM_DATA_MESSAGE,
    USBPD_DPM_EXTENDED_MESSAGE,
};

enum usbpd_dpm_event_e
{
    USBPD_DPM_ATTACHED = 0U,
    USBPD_DPM_DETACHED,
    USBPD_DPM_CONTRACT,
    USBPD_DPM_CONTRACT_LOST,
    USBPD_DPM_EPR_ENTERED,
    USBPD_DPM_EPR_EXITED,
    USBPD_DPM_HARD_RESET,
    USBPD_DPM_PROTOCOL_ERROR,
};

/* Return USBPD_OK when consumed, or USBPD_ERR_UNSUPPORTED otherwise. */
typedef int (*usbpd_dpm_message_fn)(uint8_t port, uint8_t sop, uint8_t category, uint8_t type, const uint8_t *payload,
                                    uint16_t length, void *context);

/* Fill data/length for a requested Extended Message response. */
typedef int (*usbpd_dpm_get_extended_fn)(uint8_t port, uint8_t type, uint8_t *data, uint16_t *length, void *context);

/* value0/value1 carry voltage/current for CONTRACT and zero for other events. */
typedef void (*usbpd_dpm_event_fn)(uint8_t port, uint8_t event, uint32_t value0, uint32_t value1, void *context);

/* Board power controls used only while operating as Source. */
typedef int (*usbpd_dpm_set_source_fn)(uint8_t port, uint8_t enable, uint32_t voltage_mv, uint32_t current_ma,
                                       void *context);
typedef uint8_t (*usbpd_dpm_source_ready_fn)(uint8_t port, uint32_t voltage_mv, void *context);
typedef int (*usbpd_dpm_set_switch_fn)(uint8_t port, uint8_t enable, void *context);

struct usbpd_dpm_callbacks_t
{
    usbpd_dpm_message_fn message_received;
    usbpd_dpm_get_extended_fn get_extended;
    usbpd_dpm_event_fn event;
    usbpd_dpm_set_source_fn set_source;
    usbpd_dpm_source_ready_fn source_ready;
    usbpd_dpm_set_switch_fn set_vconn;
    usbpd_dpm_set_switch_fn set_discharge;
    void *context;
};

#endif
