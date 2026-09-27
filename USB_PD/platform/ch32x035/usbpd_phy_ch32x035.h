#ifndef USBPD_PHY_CH32X035_H
#define USBPD_PHY_CH32X035_H

#include "usbpd_def.h"

void usbpd_phy_set_cc(uint8_t cc);
void usbpd_phy_set_pull(uint8_t pull);
void usbpd_phy_set_rp(uint32_t current_ma);
void usbpd_phy_set_roles(uint8_t power_role, uint8_t data_role);
void usbpd_phy_get_cc(enum usbpd_cc_e *cc1, enum usbpd_cc_e *cc2);
int usbpd_phy_get_vbus(uint32_t *mv);
int usbpd_phy_send(uint8_t sop, const uint8_t *raw, uint8_t len, uint32_t now_ms);
int usbpd_phy_receive(struct usbpd_frame_t *frame);
uint8_t usbpd_phy_tx_idle(void);
uint8_t usbpd_phy_take_rx_overflow(void);
void usbpd_phy_reset_rx(void);

int usbpd_phy_init(void);
void usbpd_phy_task(uint32_t now_ms);
#endif
