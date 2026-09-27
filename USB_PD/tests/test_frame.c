#include "usbpd_frame.h"
#include <assert.h>
#include <string.h>

static void set_header(uint8_t *wire, uint8_t type, uint8_t objects,
                       uint8_t extended)
{
    union usbpd_header_u header;

    header.raw = 0U;
    header.bits.type = type;
    header.bits.revision = USBPD_REV30;
    header.bits.data_objects = objects;
    header.bits.extended = extended;
    wire[0] = header.bytes[0];
    wire[1] = header.bytes[1];
}

int main(void)
{
    uint8_t wire[USBPD_TRACE_FRAME_LEN];
    struct usbpd_frame_t frame;

    memset(wire, 0, sizeof(wire));
    set_header(wire, USBPD_CTRL_ACCEPT, 0U, 0U);
    assert(usbpd_frame_wire_length_valid(wire, 6U) == 1U);
    assert(usbpd_frame_decode_wire(wire, 6U, USBPD_SOP, &frame) == USBPD_OK);
    assert(frame.header.bits.type == USBPD_CTRL_ACCEPT);
    assert(frame.payload_len == 0U);
    assert(frame.raw_len == 6U);

    set_header(wire, USBPD_DATA_SOURCE_CAP, 2U, 0U);
    wire[2] = 0x11U;
    wire[9] = 0x88U;
    assert(usbpd_frame_wire_length_valid(wire, 14U) == 1U);
    assert(usbpd_frame_decode_wire(wire, 14U, USBPD_SOP_PRIME, &frame) == USBPD_OK);
    assert(frame.payload_len == 2U);
    assert(frame.payload[0] == 0x11U);
    assert(frame.payload[7] == 0x88U);

    assert(usbpd_frame_wire_length_valid(wire, 13U) == 0U);
    assert(usbpd_frame_wire_length_valid(wire, 15U) == 0U);
    assert(usbpd_frame_decode_wire(wire, 13U, USBPD_SOP, &frame) == USBPD_ERR_PROTOCOL);
    assert(frame.sop == USBPD_SOP_INVALID);

    set_header(wire, USBPD_EXT_STATUS, 0U, 1U);
    assert(usbpd_frame_wire_length_valid(wire, 6U) == 0U);

    set_header(wire, USBPD_DATA_SOURCE_CAP, 7U, 0U);
    assert(usbpd_frame_wire_length_valid(wire, USBPD_TRACE_FRAME_LEN) == 1U);
    assert(usbpd_frame_wire_length_valid(wire, USBPD_TRACE_FRAME_LEN - 1U) == 0U);
    assert(usbpd_frame_wire_length_valid(wire, USBPD_TRACE_FRAME_LEN + 1U) == 0U);
    assert(usbpd_frame_decode_wire(0, 6U, USBPD_SOP, &frame) == USBPD_ERR_PARAM);
    assert(usbpd_frame_decode_wire(wire, USBPD_TRACE_FRAME_LEN,
                                   USBPD_SOP_HARD_RESET, &frame) == USBPD_ERR_PARAM);
    return 0;
}
