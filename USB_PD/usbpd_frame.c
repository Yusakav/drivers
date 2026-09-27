#include "usbpd_frame.h"
#include <string.h>

uint8_t usbpd_frame_wire_length_valid(const uint8_t *wire, uint8_t wire_length)
{
    union usbpd_header_u header;
    uint8_t expected_length;

    if ((wire == 0) || (wire_length < (2U + USBPD_CRC_LEN)) || (wire_length > USBPD_TRACE_FRAME_LEN))
    {
        return 0U;
    }

    header.bytes[0] = wire[0];
    header.bytes[1] = wire[1];
    expected_length = (uint8_t)(2U + (header.bits.data_objects * 4U) + USBPD_CRC_LEN);
    if (wire_length != expected_length)
    {
        return 0U;
    }

    /* Every Extended Message carries at least its two-byte Extended Header. */
    if ((header.bits.extended != 0U) && (header.bits.data_objects == 0U))
    {
        return 0U;
    }
    return 1U;
}

int usbpd_frame_decode_wire(const uint8_t *wire, uint8_t wire_length, uint8_t sop, struct usbpd_frame_t *frame)
{
    uint8_t payload_length;

    if ((wire == 0) || (frame == 0) || (sop > USBPD_SOP_DPRIME))
    {
        return USBPD_ERR_PARAM;
    }

    memset(frame, 0, sizeof(*frame));
    frame->sop = USBPD_SOP_INVALID;
    if (usbpd_frame_wire_length_valid(wire, wire_length) == 0U)
    {
        return USBPD_ERR_PROTOCOL;
    }

    frame->header.bytes[0] = wire[0];
    frame->header.bytes[1] = wire[1];
    payload_length = (uint8_t)(frame->header.bits.data_objects * 4U);
    if (payload_length != 0U)
    {
        memcpy(frame->payload, &wire[2], payload_length);
    }
    frame->payload_len = frame->header.bits.data_objects;
    frame->sop = sop;
    frame->raw_len = wire_length;
    return USBPD_OK;
}
