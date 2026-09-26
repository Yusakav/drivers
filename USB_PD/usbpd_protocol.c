#include "usbpd_protocol.h"
#include "usbpd_device.h"
#include <string.h>

#define USBPD_SENDER_RESPONSE_MS 30U
#define USBPD_EXT_CHUNK_DATA 26U

struct usbpd_protocol_t {
    usbpd_protocol_handler_t handler;
    void *handler_arg;
    uint8_t tx_id[3];
    uint8_t rx_id[3];
    uint8_t rx_id_valid : 3;
    uint8_t awaiting_crc : 1;
    uint8_t ext_tx_active : 1;
    uint8_t ext_rx_active : 1;
    uint8_t reserved : 2;
    uint8_t awaiting_sop;
    uint8_t awaiting_id;
    uint8_t ext_tx_type;
    uint8_t ext_tx_chunk;
    uint8_t ext_tx_request_pending : 1;
    uint8_t ext_rx_request_pending : 1;
    uint8_t ext_rx_type : 5;
    uint8_t ext_rx_chunk;
    uint8_t ext_rx_sop;
    uint16_t ext_tx_length;
    uint16_t ext_tx_offset;
    uint16_t ext_rx_length;
    uint16_t ext_rx_offset;
    uint32_t deadline_ms;
    uint8_t ext_data[USBPD_EXT_DATA_MAX];
};

static struct usbpd_protocol_t g_protocol;

static uint8_t sop_index(uint8_t sop) { return (sop <= USBPD_SOP_DPRIME) ? sop : 0U; }

static uint8_t deadline_expired(uint32_t now, uint32_t deadline)
{
    return ((int32_t)(now - deadline) >= 0);
}

static void protocol_emit(uint8_t event, const struct usbpd_protocol_msg_t *msg)
{
    if (g_protocol.handler != 0) g_protocol.handler(event, msg, g_protocol.handler_arg);
}

static int protocol_send_frame(uint8_t sop, union usbpd_header_u header,
                               const uint8_t *payload, uint8_t bytes, uint32_t now_ms)
{
    uint8_t raw[USBPD_MAX_FRAME_LEN];
    uint8_t index = sop_index(sop);
    int ret;
    if ((bytes > (USBPD_MAX_DATA_OBJ * 4U)) || ((bytes != 0U) && (payload == 0))) return USBPD_ERR;
    raw[0] = header.bytes[0];
    raw[1] = header.bytes[1];
    if (bytes != 0U) memcpy(&raw[2], payload, bytes);
    ret = usbpd_device_send(sop, raw, (uint8_t)(bytes + 2U), now_ms);
    if (ret == USBPD_OK) {
        g_protocol.awaiting_crc = 1U;
        g_protocol.awaiting_sop = sop;
        g_protocol.awaiting_id = header.bits.message_id;
        g_protocol.deadline_ms = now_ms + USBPD_SENDER_RESPONSE_MS;
        g_protocol.tx_id[index] = (uint8_t)((g_protocol.tx_id[index] + 1U) & 0x07U);
    }
    return ret;
}

static int protocol_send_chunk(uint32_t now_ms)
{
    union usbpd_header_u header;
    union usbpd_ext_header_u ext;
    uint8_t payload[USBPD_MAX_DATA_OBJ * 4U];
    uint16_t remaining;
    uint8_t data_bytes;
    uint8_t object_count;
    if ((!g_protocol.ext_tx_active) || (g_protocol.awaiting_crc != 0U)) return USBPD_BUSY;
    remaining = (uint16_t)(g_protocol.ext_tx_length - g_protocol.ext_tx_offset);
    data_bytes = (remaining > USBPD_EXT_CHUNK_DATA) ? USBPD_EXT_CHUNK_DATA : (uint8_t)remaining;
    memset(payload, 0, sizeof(payload));
    ext.raw = 0U;
    ext.bits.data_size = g_protocol.ext_tx_length;
    ext.bits.chunk_number = g_protocol.ext_tx_chunk;
    ext.bits.chunked = (g_protocol.ext_tx_length > USBPD_EXT_CHUNK_DATA);
    payload[0] = (uint8_t)(ext.raw & 0xffU);
    payload[1] = (uint8_t)(ext.raw >> 8);
    memcpy(&payload[2], &g_protocol.ext_data[g_protocol.ext_tx_offset], data_bytes);
    object_count = (uint8_t)((data_bytes + 2U + 3U) / 4U);
    header.raw = 0U;
    header.bits.type = g_protocol.ext_tx_type;
    header.bits.revision = USBPD_REV30;
    header.bits.message_id = g_protocol.tx_id[0];
    header.bits.extended = 1U;
    header.bits.data_objects = object_count;
    if (protocol_send_frame(USBPD_SOP, header, payload, (uint8_t)(object_count * 4U), now_ms) == USBPD_OK) {
        g_protocol.ext_tx_offset = (uint16_t)(g_protocol.ext_tx_offset + data_bytes);
        g_protocol.ext_tx_chunk++;
        return USBPD_OK;
    }
    return USBPD_BUSY;
}

static int protocol_request_next_chunk(uint32_t now_ms)
{
    union usbpd_header_u header;
    union usbpd_ext_header_u ext;
    uint8_t payload[4] = { 0U, 0U, 0U, 0U };
    if ((g_protocol.ext_rx_request_pending == 0U) || (g_protocol.awaiting_crc != 0U)) return USBPD_BUSY;
    ext.raw = 0U;
    ext.bits.request_chunk = 1U;
    ext.bits.chunked = 1U;
    ext.bits.chunk_number = g_protocol.ext_rx_chunk;
    payload[0] = (uint8_t)ext.raw;
    payload[1] = (uint8_t)(ext.raw >> 8);
    header.raw = 0U;
    header.bits.type = g_protocol.ext_rx_type;
    header.bits.revision = USBPD_REV30;
    header.bits.message_id = g_protocol.tx_id[sop_index(g_protocol.ext_rx_sop)];
    header.bits.data_objects = 1U;
    header.bits.extended = 1U;
    if (protocol_send_frame(g_protocol.ext_rx_sop, header, payload, sizeof(payload), now_ms) == USBPD_OK) {
        g_protocol.ext_rx_request_pending = 0U;
        return USBPD_OK;
    }
    return USBPD_BUSY;
}

void usbpd_protocol_init(usbpd_protocol_handler_t handler, void *arg)
{
    memset(&g_protocol, 0, sizeof(g_protocol));
    g_protocol.handler = handler;
    g_protocol.handler_arg = arg;
}

void usbpd_protocol_reset(void)
{
    uint8_t keep_handler = (g_protocol.handler != 0);
    usbpd_protocol_handler_t handler = g_protocol.handler;
    void *arg = g_protocol.handler_arg;
    memset(&g_protocol, 0, sizeof(g_protocol));
    if (keep_handler != 0U) {
        g_protocol.handler = handler;
        g_protocol.handler_arg = arg;
    }
}

int usbpd_protocol_send_ctrl(uint8_t sop, uint8_t type, uint32_t now_ms)
{
    union usbpd_header_u header;
    if (g_protocol.awaiting_crc != 0U) return USBPD_BUSY;
    header.raw = 0U;
    header.bits.type = type;
    header.bits.revision = USBPD_REV30;
    header.bits.message_id = g_protocol.tx_id[sop_index(sop)];
    return protocol_send_frame(sop, header, 0, 0U, now_ms);
}

int usbpd_protocol_send_data(uint8_t sop, uint8_t type, const uint32_t *objects,
                             uint8_t count, uint32_t now_ms)
{
    union usbpd_header_u header;
    if ((g_protocol.awaiting_crc != 0U) || (count == 0U) || (count > USBPD_MAX_DATA_OBJ) || (objects == 0)) return USBPD_BUSY;
    header.raw = 0U;
    header.bits.type = type;
    header.bits.revision = USBPD_REV30;
    header.bits.message_id = g_protocol.tx_id[sop_index(sop)];
    header.bits.data_objects = count;
    return protocol_send_frame(sop, header, (const uint8_t *)objects, (uint8_t)(count * 4U), now_ms);
}

int usbpd_protocol_send_extended(uint8_t type, const uint8_t *data, uint16_t length,
                                 uint32_t now_ms)
{
    if ((data == 0) || (length == 0U) || (length > USBPD_EXT_DATA_MAX) ||
        (g_protocol.ext_tx_active != 0U) || (g_protocol.awaiting_crc != 0U)) return USBPD_BUSY;
    memcpy(g_protocol.ext_data, data, length);
    g_protocol.ext_tx_active = 1U;
    g_protocol.ext_tx_type = type;
    g_protocol.ext_tx_length = length;
    g_protocol.ext_tx_offset = 0U;
    g_protocol.ext_tx_chunk = 0U;
    return protocol_send_chunk(now_ms);
}

int usbpd_protocol_send_hard_reset(uint32_t now_ms)
{
    g_protocol.awaiting_crc = 0U;
    return usbpd_device_send(USBPD_SOP_HARD_RESET, 0, 0U, now_ms);
}

static void protocol_handle_extended(const struct usbpd_frame_t *frame)
{
    union usbpd_ext_header_u ext;
    struct usbpd_protocol_msg_t msg;
    uint8_t chunk_bytes;
    uint16_t offset;
    if (frame->payload_len == 0U) return;
    ext.raw = (uint16_t)frame->payload[0] | ((uint16_t)frame->payload[1] << 8);
    if ((ext.bits.data_size == 0U) || (ext.bits.data_size > USBPD_EXT_DATA_MAX)) return;
    if (ext.bits.request_chunk != 0U) {
        if ((g_protocol.ext_tx_active != 0U) && (frame->sop == USBPD_SOP) &&
            (ext.bits.chunk_number == g_protocol.ext_tx_chunk)) {
            g_protocol.ext_tx_request_pending = 1U;
        }
        return;
    }
    chunk_bytes = (uint8_t)(frame->payload_len * 4U - 2U);
    offset = (uint16_t)ext.bits.chunk_number * USBPD_EXT_CHUNK_DATA;
    if ((offset >= ext.bits.data_size) || (offset >= USBPD_EXT_DATA_MAX)) return;
    if (chunk_bytes > (uint8_t)(ext.bits.data_size - offset)) {
        chunk_bytes = (uint8_t)(ext.bits.data_size - offset);
    }
    if ((ext.bits.chunked == 0U) || (ext.bits.chunk_number == 0U)) {
        g_protocol.ext_rx_length = ext.bits.data_size;
        g_protocol.ext_rx_offset = 0U;
        g_protocol.ext_rx_active = ext.bits.chunked;
    }
    memcpy(&g_protocol.ext_data[offset], &frame->payload[2], chunk_bytes);
    if ((uint16_t)(offset + chunk_bytes) > g_protocol.ext_rx_offset) g_protocol.ext_rx_offset = (uint16_t)(offset + chunk_bytes);
    if (g_protocol.ext_rx_offset < g_protocol.ext_rx_length) {
        g_protocol.ext_rx_type = frame->header.bits.type;
        g_protocol.ext_rx_chunk = (uint8_t)(ext.bits.chunk_number + 1U);
        g_protocol.ext_rx_sop = frame->sop;
        g_protocol.ext_rx_request_pending = 1U;
        return;
    }
    msg.header = frame->header;
    msg.payload = g_protocol.ext_data;
    msg.length = g_protocol.ext_rx_length;
    msg.sop = frame->sop;
    g_protocol.ext_rx_active = 0U;
    protocol_emit(USBPD_PROTOCOL_EXT_RX, &msg);
}

static void protocol_handle_rx(const struct usbpd_frame_t *frame)
{
    struct usbpd_protocol_msg_t msg;
    uint8_t index;
    if (frame->sop == USBPD_SOP_HARD_RESET) {
        protocol_emit(USBPD_PROTOCOL_HARD_RESET, 0);
        return;
    }
    if ((frame->sop > USBPD_SOP_DPRIME) || (frame->sop == USBPD_SOP_INVALID)) return;
    index = sop_index(frame->sop);
    if ((frame->header.bits.data_objects == 0U) && (frame->header.bits.type == USBPD_CTRL_GOODCRC)) {
        if ((g_protocol.awaiting_crc != 0U) && (g_protocol.awaiting_sop == frame->sop) &&
            (g_protocol.awaiting_id == frame->header.bits.message_id)) {
            g_protocol.awaiting_crc = 0U;
            protocol_emit(USBPD_PROTOCOL_TX_GOODCRC, 0);
        }
        return;
    }
    if (((g_protocol.rx_id_valid & (uint8_t)(1U << index)) != 0U) &&
        (g_protocol.rx_id[index] == frame->header.bits.message_id)) return;
    g_protocol.rx_id[index] = frame->header.bits.message_id;
    g_protocol.rx_id_valid |= (uint8_t)(1U << index);
    if ((frame->header.bits.data_objects == 0U) && (frame->header.bits.type == USBPD_CTRL_SOFT_RESET)) {
        g_protocol.tx_id[index] = 0U;
        g_protocol.rx_id_valid &= (uint8_t)~(1U << index);
        protocol_emit(USBPD_PROTOCOL_SOFT_RESET, 0);
        return;
    }
    if (frame->header.bits.extended != 0U) {
        protocol_handle_extended(frame);
        return;
    }
    msg.header = frame->header;
    msg.payload = frame->payload;
    msg.length = (uint16_t)frame->payload_len * 4U;
    msg.sop = frame->sop;
    protocol_emit(USBPD_PROTOCOL_RX, &msg);
}

void usbpd_protocol_task(uint32_t now_ms)
{
    struct usbpd_frame_t frame;

    if (usbpd_device_take_rx_overflow() != 0U) {
        usbpd_device_reset_rx();
        usbpd_protocol_reset();
        protocol_emit(USBPD_PROTOCOL_RX_OVERFLOW, 0);
        return;
    }

    while (usbpd_device_receive(&frame) == USBPD_OK) protocol_handle_rx(&frame);
    if ((g_protocol.awaiting_crc != 0U) && deadline_expired(now_ms, g_protocol.deadline_ms)) {
        g_protocol.awaiting_crc = 0U;
        protocol_emit(USBPD_PROTOCOL_TX_TIMEOUT, 0);
    }
    if ((g_protocol.ext_tx_active != 0U) && (g_protocol.awaiting_crc == 0U)) {
        if (g_protocol.ext_tx_offset >= g_protocol.ext_tx_length) {
            g_protocol.ext_tx_active = 0U;
        } else if (g_protocol.ext_tx_request_pending != 0U) {
            g_protocol.ext_tx_request_pending = 0U;
            (void)protocol_send_chunk(now_ms);
        }
    }
    if ((g_protocol.ext_rx_request_pending != 0U) && (g_protocol.awaiting_crc == 0U)) {
        (void)protocol_request_next_chunk(now_ms);
    }
}
