#ifndef USBPD_PROTOCOL_H
#define USBPD_PROTOCOL_H

#include "usbpd_types.h"

enum usbpd_protocol_event_e {
    USBPD_PROTOCOL_RX = 0U,
    USBPD_PROTOCOL_TX_GOODCRC,
    USBPD_PROTOCOL_TX_TIMEOUT,
    USBPD_PROTOCOL_SOFT_RESET,
    USBPD_PROTOCOL_HARD_RESET,
    USBPD_PROTOCOL_EXT_RX,
    USBPD_PROTOCOL_RX_OVERFLOW,
};

struct usbpd_protocol_msg_t {
    union usbpd_header_u header;
    const uint8_t *payload;
    uint16_t length;
    uint8_t sop;
};

typedef void (*usbpd_protocol_handler_t)(uint8_t event,
                                         const struct usbpd_protocol_msg_t *msg,
                                         void *arg);

void usbpd_protocol_init(usbpd_protocol_handler_t handler, void *arg);
void usbpd_protocol_task(uint32_t now_ms);
int usbpd_protocol_send_ctrl(uint8_t sop, uint8_t type, uint32_t now_ms);
int usbpd_protocol_send_data(uint8_t sop, uint8_t type, const uint32_t *objects,
                             uint8_t count, uint32_t now_ms);
int usbpd_protocol_send_extended(uint8_t type, const uint8_t *data, uint16_t length,
                                 uint32_t now_ms);
int usbpd_protocol_send_hard_reset(uint32_t now_ms);
void usbpd_protocol_reset(void);

#endif
