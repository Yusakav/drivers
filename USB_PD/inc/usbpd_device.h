#ifndef USBPD_DEVICE_H
#define USBPD_DEVICE_H

#include "usbpd_types.h"

int usbpd_device_init(void);
void usbpd_device_task(uint32_t now_ms);
void usbpd_device_set_cc(uint8_t cc);
void usbpd_device_get_cc(enum usbpd_cc_e *cc1, enum usbpd_cc_e *cc2);
int usbpd_device_get_vbus(uint32_t *mv);
int usbpd_device_send(uint8_t sop, const uint8_t *raw, uint8_t len, uint32_t now_ms);
int usbpd_device_receive(struct usbpd_frame_t *frame);
uint8_t usbpd_device_tx_idle(void);
uint8_t usbpd_device_take_rx_overflow(void);
void usbpd_device_reset_rx(void);

#endif
