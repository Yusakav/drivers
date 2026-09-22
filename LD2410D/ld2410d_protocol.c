/**
 * @file ld2410d_protocol.c
 * @brief LD2410D frame construction and parsing.
 */

#include "ld2410d_protocol.h"
#include <string.h>

#define LD2410D_ACK_PAYLOAD_OFFSET       10U
#define LD2410D_ACK_FIXED_DATA_LEN        4U
#define LD2410D_ENGINEERING_DATA_LEN    131U
#define LD2410D_ENGINEERING_TOTAL_LEN   141U

uint16_t ld2410d_get_u16_le(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

uint32_t ld2410d_get_u32_le(const uint8_t *buf)
{
    return (uint32_t)buf[0]
           | ((uint32_t)buf[1] << 8)
           | ((uint32_t)buf[2] << 16)
           | ((uint32_t)buf[3] << 24);
}

void ld2410d_put_u16_le(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value & 0xFFU);
    buf[1] = (uint8_t)((value >> 8) & 0xFFU);
}

void ld2410d_put_u32_le(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value & 0xFFU);
    buf[1] = (uint8_t)((value >> 8) & 0xFFU);
    buf[2] = (uint8_t)((value >> 16) & 0xFFU);
    buf[3] = (uint8_t)((value >> 24) & 0xFFU);
}

ld2410d_ret_t ld2410d_build_command_frame(uint8_t *buf, uint16_t buf_size,
                                           uint16_t cmd,
                                           const uint8_t *payload,
                                           uint16_t payload_len,
                                           uint16_t *frame_len)
{
    uint32_t total_len;
    uint8_t *p;

    if (buf == NULL || frame_len == NULL) {
        return LD2410D_ERR_NULL;
    }
    if (payload_len > 0U && payload == NULL) {
        return LD2410D_ERR_NULL;
    }

    total_len = (uint32_t)LD2410D_FRAME_OVERHEAD + 2U + payload_len;
    if ((uint32_t)buf_size < total_len) {
        return LD2410D_ERR_OVERFLOW;
    }

    p = buf;
    *p++ = LD2410D_FRAME_HEADER_0;
    *p++ = LD2410D_FRAME_HEADER_1;
    *p++ = LD2410D_FRAME_HEADER_2;
    *p++ = LD2410D_FRAME_HEADER_3;
    ld2410d_put_u16_le(p, (uint16_t)(2U + payload_len));
    p += 2;
    ld2410d_put_u16_le(p, cmd);
    p += 2;

    if (payload_len > 0U) {
        memcpy(p, payload, payload_len);
        p += payload_len;
    }

    *p++ = LD2410D_FRAME_FOOTER_0;
    *p++ = LD2410D_FRAME_FOOTER_1;
    *p++ = LD2410D_FRAME_FOOTER_2;
    *p++ = LD2410D_FRAME_FOOTER_3;

    *frame_len = (uint16_t)(p - buf);
    return LD2410D_OK;
}

ld2410d_ret_t ld2410d_parse_ack_frame(const uint8_t *buf, uint16_t len,
                                       uint16_t expected_cmd,
                                       ld2410d_ack_frame_t *ack)
{
    uint16_t frame_data_len;
    uint32_t total_len;
    const uint8_t *footer;
    uint16_t cmd;
    uint16_t ack_status;

    if (buf == NULL || ack == NULL) {
        return LD2410D_ERR_NULL;
    }
    if (len < (LD2410D_FRAME_OVERHEAD + LD2410D_ACK_FIXED_DATA_LEN)) {
        return LD2410D_ERR_FRAME;
    }
    if (buf[0] != LD2410D_FRAME_HEADER_0 ||
        buf[1] != LD2410D_FRAME_HEADER_1 ||
        buf[2] != LD2410D_FRAME_HEADER_2 ||
        buf[3] != LD2410D_FRAME_HEADER_3) {
        return LD2410D_ERR_FRAME;
    }

    frame_data_len = ld2410d_get_u16_le(&buf[4]);
    if (frame_data_len < LD2410D_ACK_FIXED_DATA_LEN) {
        return LD2410D_ERR_FRAME;
    }

    total_len = 6U + (uint32_t)frame_data_len + 4U;
    if (total_len > (uint32_t)len) {
        return LD2410D_ERR_FRAME;
    }

    footer = buf + 6U + frame_data_len;
    if (footer[0] != LD2410D_FRAME_FOOTER_0 ||
        footer[1] != LD2410D_FRAME_FOOTER_1 ||
        footer[2] != LD2410D_FRAME_FOOTER_2 ||
        footer[3] != LD2410D_FRAME_FOOTER_3) {
        return LD2410D_ERR_FRAME;
    }

    cmd = ld2410d_get_u16_le(&buf[6]);
    if (cmd != expected_cmd) {
        return LD2410D_ERR_FRAME;
    }

    ack_status = ld2410d_get_u16_le(&buf[8]);
    if (ack_status != 0U) {
        return LD2410D_ERR_ACK;
    }

    ack->cmd = cmd;
    ack->ack_status = ack_status;
    ack->payload = &buf[LD2410D_ACK_PAYLOAD_OFFSET];
    ack->payload_len = (uint16_t)(frame_data_len - LD2410D_ACK_FIXED_DATA_LEN);
    return LD2410D_OK;
}

ld2410d_ret_t ld2410d_parse_engineering_frame(const uint8_t *buf,
                                               uint16_t len,
                                               ld2410d_engineering_data_t *data)
{
    uint16_t frame_data_len;
    uint32_t total_len;
    const uint8_t *footer;
    const uint8_t *p;
    uint8_t status;
    uint8_t i;

    if (buf == NULL || data == NULL) {
        return LD2410D_ERR_NULL;
    }
    if (len < LD2410D_ENGINEERING_TOTAL_LEN) {
        return LD2410D_ERR_FRAME;
    }
    if (buf[0] != LD2410D_DATA_HEADER_0 ||
        buf[1] != LD2410D_DATA_HEADER_1 ||
        buf[2] != LD2410D_DATA_HEADER_2 ||
        buf[3] != LD2410D_DATA_HEADER_3) {
        return LD2410D_ERR_FRAME;
    }

    frame_data_len = ld2410d_get_u16_le(&buf[4]);
    if (frame_data_len < LD2410D_ENGINEERING_DATA_LEN) {
        return LD2410D_ERR_FRAME;
    }

    total_len = 6U + (uint32_t)frame_data_len + 4U;
    if (total_len > (uint32_t)len) {
        return LD2410D_ERR_FRAME;
    }

    footer = buf + 6U + frame_data_len;
    if (footer[0] != LD2410D_DATA_FOOTER_0 ||
        footer[1] != LD2410D_DATA_FOOTER_1 ||
        footer[2] != LD2410D_DATA_FOOTER_2 ||
        footer[3] != LD2410D_DATA_FOOTER_3) {
        return LD2410D_ERR_FRAME;
    }

    status = buf[6];
    if (status > (uint8_t)LD2410D_DETECT_STATIC) {
        status = (uint8_t)LD2410D_DETECT_NONE;
    }

    data->status = (ld2410d_detect_status_t)status;
    data->distance = ld2410d_get_u16_le(&buf[7]);

    p = &buf[9];
    for (i = 0U; i < LD2410D_GATE_COUNT; i++) {
        data->gates[i].motion_energy = ld2410d_get_u32_le(p);
        p += 4;
    }
    for (i = 0U; i < LD2410D_GATE_COUNT; i++) {
        data->gates[i].static_energy = ld2410d_get_u32_le(p);
        p += 4;
    }

    return LD2410D_OK;
}
