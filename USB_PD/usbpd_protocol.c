#include "usbpd_protocol.h"
#include "usbpd_phy_ch32x035.h"
#include <string.h>

#define USBPD_GOODCRC_TIMEOUT_MS 2U

struct usbpd_protocol_t
{
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
    uint8_t tx_retry_count;
    uint8_t tx_raw_len;
    uint8_t tx_raw[USBPD_MAX_FRAME_LEN];
    uint8_t revision;
    uint8_t power_role;
    uint8_t data_role;
    uint8_t ext_tx_type;
    uint8_t ext_tx_sop;
    uint8_t ext_tx_chunk;
    uint8_t ext_tx_request_pending : 1;
    uint8_t ext_tx_wait_request : 1;
    uint8_t ext_rx_request_pending : 1;
    uint8_t ext_rx_wait_chunk : 1;
    uint8_t reserved_flags : 4;
    uint8_t ext_rx_type;
    uint8_t ext_rx_chunk;
    uint8_t ext_rx_sop;
    uint16_t ext_tx_length;
    uint16_t ext_tx_offset;
    uint16_t ext_rx_length;
    uint16_t ext_rx_offset;
    uint32_t deadline_ms;
    uint32_t ext_deadline_ms;
    uint32_t now_ms;
    uint8_t ext_tx_data[USBPD_EXT_DATA_MAX];
    uint8_t ext_rx_data[USBPD_EXT_DATA_MAX];
};

static struct usbpd_protocol_t g_protocol;

static uint8_t sop_index(uint8_t sop)
{
    return (sop <= USBPD_SOP_DPRIME) ? sop : 0U;
}

static uint8_t deadline_expired(uint32_t now, uint32_t deadline)
{
    return ((int32_t)(now - deadline) >= 0);
}

static void protocol_emit(uint8_t event, const struct usbpd_protocol_msg_t *msg)
{
    if (g_protocol.handler != 0)
        g_protocol.handler(event, msg, g_protocol.handler_arg);
}

static void protocol_emit_tx_goodcrc(void)
{
    struct usbpd_protocol_msg_t msg;
    msg.header.bytes[0] = g_protocol.tx_raw[0];
    msg.header.bytes[1] = g_protocol.tx_raw[1];
    msg.payload = (g_protocol.tx_raw_len > 2U) ? &g_protocol.tx_raw[2] : 0;
    msg.length = (g_protocol.tx_raw_len > 2U) ? (uint16_t)(g_protocol.tx_raw_len - 2U) : 0U;
    msg.sop = g_protocol.awaiting_sop;
    protocol_emit(USBPD_PROTOCOL_TX_GOODCRC, &msg);
}

static void protocol_fill_header(union usbpd_header_u *header, uint8_t sop, uint8_t type, uint8_t objects,
                                 uint8_t extended)
{
    uint8_t index = sop_index(sop);
    header->raw = 0U;
    header->bits.type = type;
    header->bits.revision = g_protocol.revision;
    header->bits.message_id = g_protocol.tx_id[index];
    header->bits.data_objects = objects;
    header->bits.extended = extended;
    if (sop == USBPD_SOP)
    {
        header->bits.power_role = g_protocol.power_role;
        header->bits.data_role = g_protocol.data_role;
    }
}

static int protocol_send_frame(uint8_t sop, union usbpd_header_u header, const uint8_t *payload, uint8_t bytes,
                               uint32_t now_ms)
{
    uint8_t raw[USBPD_MAX_FRAME_LEN];
    int ret;
    if ((bytes > (USBPD_MAX_DATA_OBJ * 4U)) || ((bytes != 0U) && (payload == 0)))
        return USBPD_ERR;
    raw[0] = header.bytes[0];
    raw[1] = header.bytes[1];
    if (bytes != 0U)
        memcpy(&raw[2], payload, bytes);
    ret = usbpd_phy_send(sop, raw, (uint8_t)(bytes + 2U), now_ms);
    if (ret == USBPD_OK)
    {
        g_protocol.awaiting_crc = 1U;
        g_protocol.awaiting_sop = sop;
        g_protocol.awaiting_id = header.bits.message_id;
        g_protocol.deadline_ms = now_ms + USBPD_GOODCRC_TIMEOUT_MS;
        g_protocol.tx_retry_count = 0U;
        g_protocol.tx_raw_len = (uint8_t)(bytes + 2U);
        memcpy(g_protocol.tx_raw, raw, g_protocol.tx_raw_len);
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
    if ((!g_protocol.ext_tx_active) || (g_protocol.awaiting_crc != 0U))
        return USBPD_BUSY;
    remaining = (uint16_t)(g_protocol.ext_tx_length - g_protocol.ext_tx_offset);
    data_bytes = (remaining > USBPD_EXT_CHUNK_DATA_MAX) ? USBPD_EXT_CHUNK_DATA_MAX : (uint8_t)remaining;
    memset(payload, 0, sizeof(payload));
    ext.raw = 0U;
    ext.bits.data_size = g_protocol.ext_tx_length;
    ext.bits.chunk_number = g_protocol.ext_tx_chunk;
    ext.bits.chunked = (g_protocol.ext_tx_length > USBPD_EXT_CHUNK_DATA_MAX);
    payload[0] = (uint8_t)(ext.raw & 0xffU);
    payload[1] = (uint8_t)(ext.raw >> 8);
    memcpy(&payload[2], &g_protocol.ext_tx_data[g_protocol.ext_tx_offset], data_bytes);
    object_count = (uint8_t)((data_bytes + 2U + 3U) / 4U);
    protocol_fill_header(&header, g_protocol.ext_tx_sop, g_protocol.ext_tx_type, object_count, 1U);
    if (protocol_send_frame(g_protocol.ext_tx_sop, header, payload, (uint8_t)(object_count * 4U), now_ms) == USBPD_OK)
    {
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
    uint8_t payload[4] = {0U, 0U, 0U, 0U};
    if ((g_protocol.ext_rx_request_pending == 0U) || (g_protocol.awaiting_crc != 0U))
        return USBPD_BUSY;
    ext.raw = 0U;
    ext.bits.request_chunk = 1U;
    ext.bits.chunked = 1U;
    ext.bits.chunk_number = g_protocol.ext_rx_chunk;
    payload[0] = (uint8_t)ext.raw;
    payload[1] = (uint8_t)(ext.raw >> 8);
    protocol_fill_header(&header, g_protocol.ext_rx_sop, g_protocol.ext_rx_type, 1U, 1U);
    if (protocol_send_frame(g_protocol.ext_rx_sop, header, payload, sizeof(payload), now_ms) == USBPD_OK)
    {
        g_protocol.ext_rx_request_pending = 0U;
        g_protocol.ext_rx_wait_chunk = 1U;
        g_protocol.ext_deadline_ms = now_ms + USBPD_T_CHUNK_SENDER_RESPONSE_MS;
        return USBPD_OK;
    }
    return USBPD_BUSY;
}

static void protocol_handle_extended(const struct usbpd_frame_t *frame)
{
    union usbpd_ext_header_u ext;
    struct usbpd_protocol_msg_t msg;
    uint8_t chunk_bytes;
    uint16_t offset;
    if (frame->payload_len == 0U)
    {
        protocol_emit(USBPD_PROTOCOL_ERROR, 0);
        return;
    }
    ext.raw = (uint16_t)frame->payload[0] | ((uint16_t)frame->payload[1] << 8);
    if (ext.bits.request_chunk != 0U)
    {
        if ((ext.bits.chunked == 0U) || (ext.bits.data_size != 0U))
        {
            protocol_emit(USBPD_PROTOCOL_ERROR, 0);
            return;
        }
        if ((g_protocol.ext_tx_active == 0U) || (frame->sop != g_protocol.ext_tx_sop) ||
            (frame->header.bits.type != g_protocol.ext_tx_type) || (ext.bits.chunk_number != g_protocol.ext_tx_chunk))
        {
            protocol_emit(USBPD_PROTOCOL_ERROR, 0);
            return;
        }
        g_protocol.ext_tx_request_pending = 1U;
        g_protocol.ext_tx_wait_request = 0U;
        return;
    }
    if ((ext.bits.data_size == 0U) || (ext.bits.data_size > USBPD_EXT_DATA_MAX))
    {
        protocol_emit(USBPD_PROTOCOL_ERROR, 0);
        return;
    }
    chunk_bytes = (uint8_t)(frame->payload_len * 4U - 2U);
    if ((ext.bits.chunked == 0U) && (ext.bits.data_size > chunk_bytes))
    {
        protocol_emit(USBPD_PROTOCOL_ERROR, 0);
        return;
    }
    if ((ext.bits.chunked != 0U) && (ext.bits.chunk_number == 0U) && (ext.bits.data_size > USBPD_EXT_CHUNK_DATA_MAX) &&
        (chunk_bytes != USBPD_EXT_CHUNK_DATA_MAX))
    {
        protocol_emit(USBPD_PROTOCOL_ERROR, 0);
        return;
    }
    offset = (uint16_t)ext.bits.chunk_number * USBPD_EXT_CHUNK_DATA_MAX;
    if ((offset >= ext.bits.data_size) || (offset >= USBPD_EXT_DATA_MAX))
        return;
    if (chunk_bytes > (uint8_t)(ext.bits.data_size - offset))
    {
        chunk_bytes = (uint8_t)(ext.bits.data_size - offset);
    }
    if ((ext.bits.chunked == 0U) || (ext.bits.chunk_number == 0U))
    {
        g_protocol.ext_rx_length = ext.bits.data_size;
        g_protocol.ext_rx_offset = 0U;
        g_protocol.ext_rx_active = ext.bits.chunked;
    }
    else if ((g_protocol.ext_rx_active == 0U) || (frame->sop != g_protocol.ext_rx_sop) ||
             (frame->header.bits.type != g_protocol.ext_rx_type) || (ext.bits.chunk_number != g_protocol.ext_rx_chunk))
    {
        protocol_emit(USBPD_PROTOCOL_ERROR, 0);
        return;
    }
    g_protocol.ext_rx_wait_chunk = 0U;
    memcpy(&g_protocol.ext_rx_data[offset], &frame->payload[2], chunk_bytes);
    if ((uint16_t)(offset + chunk_bytes) > g_protocol.ext_rx_offset)
        g_protocol.ext_rx_offset = (uint16_t)(offset + chunk_bytes);
    if (g_protocol.ext_rx_offset < g_protocol.ext_rx_length)
    {
        g_protocol.ext_rx_type = frame->header.bits.type;
        g_protocol.ext_rx_chunk = (uint8_t)(ext.bits.chunk_number + 1U);
        g_protocol.ext_rx_sop = frame->sop;
        g_protocol.ext_rx_request_pending = 1U;
        return;
    }
    msg.header = frame->header;
    msg.payload = g_protocol.ext_rx_data;
    msg.length = g_protocol.ext_rx_length;
    msg.sop = frame->sop;
    g_protocol.ext_rx_active = 0U;
    protocol_emit(USBPD_PROTOCOL_EXT_RX, &msg);
}

static void protocol_handle_rx(const struct usbpd_frame_t *frame)
{
    struct usbpd_protocol_msg_t msg;
    uint8_t index;
    if (frame->sop == USBPD_SOP_HARD_RESET)
    {
        protocol_emit(USBPD_PROTOCOL_HARD_RESET, 0);
        return;
    }
    if ((frame->sop > USBPD_SOP_DPRIME) || (frame->sop == USBPD_SOP_INVALID))
        return;
    index = sop_index(frame->sop);
    if ((frame->sop == USBPD_SOP) && (frame->header.bits.revision < g_protocol.revision))
    {
        g_protocol.revision = frame->header.bits.revision;
    }
    if ((frame->header.bits.data_objects == 0U) && (frame->header.bits.type == USBPD_CTRL_GOODCRC))
    {
        if ((g_protocol.awaiting_crc != 0U) && (g_protocol.awaiting_sop == frame->sop) &&
            (g_protocol.awaiting_id == frame->header.bits.message_id))
        {
            g_protocol.awaiting_crc = 0U;
            g_protocol.tx_id[index] = (uint8_t)((g_protocol.tx_id[index] + 1U) & 0x07U);
            if ((g_protocol.ext_tx_active != 0U) && (g_protocol.ext_tx_offset < g_protocol.ext_tx_length))
            {
                g_protocol.ext_tx_wait_request = 1U;
                g_protocol.ext_deadline_ms = g_protocol.now_ms + USBPD_T_CHUNK_SENDER_REQUEST_MS;
            }
            protocol_emit_tx_goodcrc();
        }
        return;
    }
    if (((g_protocol.rx_id_valid & (uint8_t)(1U << index)) != 0U) &&
        (g_protocol.rx_id[index] == frame->header.bits.message_id))
        return;
    g_protocol.rx_id[index] = frame->header.bits.message_id;
    g_protocol.rx_id_valid |= (uint8_t)(1U << index);
    if ((frame->header.bits.data_objects == 0U) && (frame->header.bits.type == USBPD_CTRL_SOFT_RESET))
    {
        g_protocol.tx_id[index] = 0U;
        g_protocol.rx_id_valid &= (uint8_t)~(1U << index);
        protocol_emit(USBPD_PROTOCOL_SOFT_RESET, 0);
        return;
    }
    if (frame->header.bits.extended != 0U)
    {
        protocol_handle_extended(frame);
        return;
    }
    msg.header = frame->header;
    msg.payload = frame->payload;
    msg.length = (uint16_t)frame->payload_len * 4U;
    msg.sop = frame->sop;
    protocol_emit(USBPD_PROTOCOL_RX, &msg);
}

void usbpd_protocol_configure(uint8_t revision, uint8_t power_role, uint8_t data_role)
{
    g_protocol.revision = (revision <= USBPD_REV30) ? revision : USBPD_REV30;
    g_protocol.power_role = (power_role != 0U) ? USBPD_POWER_ROLE_SOURCE : USBPD_POWER_ROLE_SINK;
    g_protocol.data_role = (data_role != 0U) ? USBPD_DATA_ROLE_DFP : USBPD_DATA_ROLE_UFP;
    usbpd_phy_set_roles(g_protocol.power_role, g_protocol.data_role);
}

int usbpd_protocol_send_ctrl(uint8_t sop, uint8_t type, uint32_t now_ms)
{
    union usbpd_header_u header;
    if ((sop > USBPD_SOP_DPRIME) || (type == USBPD_CTRL_RESERVED) || (type > USBPD_CTRL_GET_REVISION))
        return USBPD_ERR_PARAM;
    if (g_protocol.awaiting_crc != 0U)
        return USBPD_BUSY;
    protocol_fill_header(&header, sop, type, 0U, 0U);
    return protocol_send_frame(sop, header, 0, 0U, now_ms);
}

int usbpd_protocol_send_data(uint8_t sop, uint8_t type, const uint32_t *objects, uint8_t count, uint32_t now_ms)
{
    union usbpd_header_u header;
    if ((sop > USBPD_SOP_DPRIME) || (type == USBPD_DATA_RESERVED) ||
        ((type > USBPD_DATA_REVISION) && (type != USBPD_DATA_VENDOR_DEFINED)) || (count == 0U) ||
        (count > USBPD_MAX_DATA_OBJ) || (objects == 0))
        return USBPD_ERR_PARAM;
    if (g_protocol.awaiting_crc != 0U)
        return USBPD_BUSY;
    protocol_fill_header(&header, sop, type, count, 0U);
    return protocol_send_frame(sop, header, (const uint8_t *)objects, (uint8_t)(count * 4U), now_ms);
}

int usbpd_protocol_send_extended(uint8_t type, const uint8_t *data, uint16_t length, uint32_t now_ms)
{
    return usbpd_protocol_send_extended_sop(USBPD_SOP, type, data, length, now_ms);
}

int usbpd_protocol_send_extended_sop(uint8_t sop, uint8_t type, const uint8_t *data, uint16_t length, uint32_t now_ms)
{
    if ((sop > USBPD_SOP_DPRIME) || (type == USBPD_EXT_RESERVED) ||
        ((type > USBPD_EXT_EPR_SINK_CAP) && (type != USBPD_EXT_VENDOR_DEFINED)) || (data == 0) || (length == 0U) ||
        (length > USBPD_EXT_DATA_MAX))
        return USBPD_ERR_PARAM;
    if ((g_protocol.ext_tx_active != 0U) || (g_protocol.awaiting_crc != 0U))
        return USBPD_BUSY;
    memcpy(g_protocol.ext_tx_data, data, length);
    g_protocol.ext_tx_active = 1U;
    g_protocol.ext_tx_type = type;
    g_protocol.ext_tx_sop = sop;
    g_protocol.ext_tx_length = length;
    g_protocol.ext_tx_offset = 0U;
    g_protocol.ext_tx_chunk = 0U;
    return protocol_send_chunk(now_ms);
}

int usbpd_protocol_send_extended_control(uint8_t subtype, uint8_t data, uint32_t now_ms)
{
    const uint8_t ecdb[2] = {subtype, data};
    if ((subtype < USBPD_EXT_CTRL_EPR_GET_SOURCE_CAP) || (subtype > USBPD_EXT_CTRL_EPR_KEEPALIVE_ACK))
        return USBPD_ERR_PARAM;
    return usbpd_protocol_send_extended(USBPD_EXT_CONTROL, ecdb, sizeof(ecdb), now_ms);
}

int usbpd_protocol_send_hard_reset(uint32_t now_ms)
{
    g_protocol.awaiting_crc = 0U;
    return usbpd_phy_send(USBPD_SOP_HARD_RESET, 0, 0U, now_ms);
}

void usbpd_protocol_reset(void)
{
    uint8_t keep_handler = (g_protocol.handler != 0);
    usbpd_protocol_handler_t handler = g_protocol.handler;
    void *arg = g_protocol.handler_arg;
    uint8_t revision = g_protocol.revision;
    uint8_t power_role = g_protocol.power_role;
    uint8_t data_role = g_protocol.data_role;
    memset(&g_protocol, 0, sizeof(g_protocol));
    if (keep_handler != 0U)
    {
        g_protocol.handler = handler;
        g_protocol.handler_arg = arg;
    }
    g_protocol.revision = revision;
    g_protocol.power_role = power_role;
    g_protocol.data_role = data_role;
}

void usbpd_protocol_init(usbpd_protocol_handler_t handler, void *arg)
{
    memset(&g_protocol, 0, sizeof(g_protocol));
    g_protocol.handler = handler;
    g_protocol.handler_arg = arg;
    g_protocol.revision = USBPD_REV30;
}

void usbpd_protocol_task(uint32_t now_ms)
{
    struct usbpd_frame_t frame;
    int receive_result;

    g_protocol.now_ms = now_ms;

    if (usbpd_phy_take_rx_overflow() != 0U)
    {
        usbpd_phy_reset_rx();
        usbpd_protocol_reset();
        protocol_emit(USBPD_PROTOCOL_RX_OVERFLOW, 0);
        return;
    }

    do
    {
        receive_result = usbpd_phy_receive(&frame);
        if (receive_result == USBPD_OK)
            protocol_handle_rx(&frame);
    } while (receive_result == USBPD_OK);
    if (receive_result != USBPD_BUSY)
        protocol_emit(USBPD_PROTOCOL_ERROR, 0);
    if ((g_protocol.awaiting_crc != 0U) && deadline_expired(now_ms, g_protocol.deadline_ms))
    {
        if ((g_protocol.tx_retry_count < USBPD_N_RETRY_COUNT) &&
            (usbpd_phy_send(g_protocol.awaiting_sop, g_protocol.tx_raw, g_protocol.tx_raw_len, now_ms) == USBPD_OK))
        {
            g_protocol.tx_retry_count++;
            g_protocol.deadline_ms = now_ms + USBPD_GOODCRC_TIMEOUT_MS;
        }
        else if (g_protocol.tx_retry_count >= USBPD_N_RETRY_COUNT)
        {
            g_protocol.awaiting_crc = 0U;
            protocol_emit(USBPD_PROTOCOL_TX_TIMEOUT, 0);
        }
    }
    if ((g_protocol.ext_tx_active != 0U) && (g_protocol.awaiting_crc == 0U))
    {
        if (g_protocol.ext_tx_offset >= g_protocol.ext_tx_length)
        {
            g_protocol.ext_tx_active = 0U;
        }
        else if (g_protocol.ext_tx_request_pending != 0U)
        {
            g_protocol.ext_tx_request_pending = 0U;
            (void)protocol_send_chunk(now_ms);
        }
    }
    if ((g_protocol.ext_rx_request_pending != 0U) && (g_protocol.awaiting_crc == 0U))
    {
        (void)protocol_request_next_chunk(now_ms);
    }
    if (((g_protocol.ext_tx_wait_request != 0U) || (g_protocol.ext_rx_wait_chunk != 0U)) &&
        deadline_expired(now_ms, g_protocol.ext_deadline_ms))
    {
        g_protocol.ext_tx_active = 0U;
        g_protocol.ext_rx_active = 0U;
        g_protocol.ext_tx_wait_request = 0U;
        g_protocol.ext_rx_wait_chunk = 0U;
        protocol_emit(USBPD_PROTOCOL_TX_TIMEOUT, 0);
    }
}
