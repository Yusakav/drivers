/**
 * @file ld2410d_protocol.h
 * @brief LD2410D frame construction and parsing helpers.
 */

#ifndef LD2410D_PROTOCOL_H
#define LD2410D_PROTOCOL_H

#include "ld2410d.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t       cmd;
    uint16_t       ack_status;
    const uint8_t *payload;
    uint16_t       payload_len;
} ld2410d_ack_frame_t;

uint16_t ld2410d_get_u16_le(const uint8_t *buf);
uint32_t ld2410d_get_u32_le(const uint8_t *buf);
void ld2410d_put_u16_le(uint8_t *buf, uint16_t value);
void ld2410d_put_u32_le(uint8_t *buf, uint32_t value);

ld2410d_ret_t ld2410d_build_command_frame(uint8_t *buf, uint16_t buf_size,
                                           uint16_t cmd,
                                           const uint8_t *payload,
                                           uint16_t payload_len,
                                           uint16_t *frame_len);
ld2410d_ret_t ld2410d_parse_ack_frame(const uint8_t *buf, uint16_t len,
                                       uint16_t expected_cmd,
                                       ld2410d_ack_frame_t *ack);

#ifdef __cplusplus
}
#endif

#endif
