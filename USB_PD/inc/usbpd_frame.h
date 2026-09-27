#ifndef USBPD_FRAME_H
#define USBPD_FRAME_H

#include "usbpd_def.h"

/* Validate Header + Data Objects + four-byte CRC wire framing. */
uint8_t usbpd_frame_wire_length_valid(const uint8_t *wire, uint8_t wire_length);

/* Decode a CRC-validated wire slot into a protocol frame. */
int usbpd_frame_decode_wire(const uint8_t *wire, uint8_t wire_length, uint8_t sop, struct usbpd_frame_t *frame);

#endif
