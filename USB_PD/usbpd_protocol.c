/**
* @file usbpd_protocol.c
* @brief USB Power Delivery（USB PD）协议层实现
*
* 所属模块：USB PD 协议栈（驱动层，与具体控制器/平台无关）。
* 职责：
*   - 构造/解析报文头，维护各 SOP 通道的 MessageID 收发序号与去重；
*   - 发送后等待 GoodCRC，超时按 USBPD_N_RETRY_COUNT 重发并上报事件；
*   - 扩展消息分块发送/接收（Chunking），自动向对端请求下一分块；
*   - 处理 Soft Reset / Hard Reset / 接收溢出等复位流程。
*
* 事件经 usbpd_protocol_event_e 回调上报上层；由调用方周期调用
* usbpd_protocol_task()（毫秒节拍）驱动协议层运行。
*/
#include "usbpd_protocol.h"
#include "usbpd_phy_ch32x035.h"
#include <string.h>

/* ===== 协议层私有参数 ===== */
#define USBPD_GOODCRC_TIMEOUT_MS 2U  /* GoodCRC 等待超时（毫秒，覆盖 tReceive 与帧间隔） */

/* ===== 协议层私有类型与全局状态 ===== */
/** @brief USB PD 协议层运行上下文（全局单例） */
struct usbpd_protocol_t
{
    usbpd_protocol_handler_t handler;        /* 协议事件回调函数 */
    void *handler_arg;                       /* 事件回调用户参数 */
    uint8_t tx_id[3];                        /* 各 SOP 通道发送 MessageID（0..7 循环） */
    uint8_t rx_id[3];                        /* 各 SOP 通道最近接收 MessageID */
    uint8_t rx_id_valid : 3;                 /* Bit0..2 : rx_id 有效性位图（按 SOP 索引置位） */
    uint8_t awaiting_crc : 1;                /* 已发送待 GoodCRC 确认标志 */
    uint8_t ext_tx_active : 1;               /* 扩展消息分块发送进行中 */
    uint8_t ext_rx_active : 1;               /* 扩展消息分块接收进行中 */
    uint8_t reserved : 2;                    /* 保留位 */
    uint8_t awaiting_sop;                    /* 待确认帧的 SOP 类型 */
    uint8_t awaiting_id;                     /* 待确认帧的 MessageID */
    uint8_t tx_retry_count;                  /* 当前帧已重发次数 */
    uint8_t tx_raw_len;                      /* 待确认帧长度（字节，含 2 字节报文头） */
    uint8_t tx_raw[USBPD_MAX_FRAME_LEN];     /* 待确认帧原始内容（超时重发用） */
    uint8_t revision;                        /* 当前协商协议版本（见 usbpd_revision_e） */
    uint8_t power_role;                      /* 电源角色（见 usbpd_power_role_e） */
    uint8_t data_role;                       /* 数据角色（见 usbpd_data_role_e） */
    uint8_t ext_tx_type;                     /* 扩展发送消息类型（见 usbpd_extended_e） */
    uint8_t ext_tx_sop;                      /* 扩展发送 SOP 类型 */
    uint8_t ext_tx_chunk;                    /* 扩展发送下一分块序号 */
    uint8_t ext_tx_request_pending : 1;      /* 已收到 Chunk Request，待发送下一分块 */
    uint8_t ext_tx_wait_request : 1;         /* 已发分块，等待对端 Chunk Request */
    uint8_t ext_rx_request_pending : 1;      /* 待向对端发送 Chunk Request */
    uint8_t ext_rx_wait_chunk : 1;           /* 已发 Chunk Request，等待对端数据块 */
    uint8_t reserved_flags : 4;              /* 保留位 */
    uint8_t ext_rx_type;                     /* 扩展接收消息类型（见 usbpd_extended_e） */
    uint8_t ext_rx_chunk;                    /* 扩展接收下一分块序号 */
    uint8_t ext_rx_sop;                      /* 扩展接收 SOP 类型 */
    uint16_t ext_tx_length;                  /* 扩展发送数据总长度（字节） */
    uint16_t ext_tx_offset;                  /* 扩展发送已发送偏移（字节） */
    uint16_t ext_rx_length;                  /* 扩展接收数据总长度（字节） */
    uint16_t ext_rx_offset;                  /* 扩展接收已接收偏移（字节） */
    uint32_t deadline_ms;                    /* GoodCRC 等待截止时间（毫秒） */
    uint32_t ext_deadline_ms;                /* 分块传输等待截止时间（毫秒） */
    uint32_t now_ms;                         /* 最近一次 task 调度时的系统时间（毫秒） */
    uint8_t ext_tx_data[USBPD_EXT_DATA_MAX]; /* 扩展发送数据缓冲 */
    uint8_t ext_rx_data[USBPD_EXT_DATA_MAX]; /* 扩展接收重组缓冲 */
};

static struct usbpd_protocol_t g_protocol;   /* 协议层全局上下文单例 */

/* ===== 内部函数 ===== */
/**
 * @brief  将 SOP 类型映射为 MessageID 数组索引
 *
 * 仅 SOP / SOP' / SOP'' 三类通道维护 MessageID，其余值统一映射到索引 0。
 *
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @return MessageID 数组索引（0..2）
 */
static uint8_t sop_index(uint8_t sop)
{
    return (sop <= USBPD_SOP_DPRIME) ? sop : 0U;
}

/**
 * @brief  判断截止时间是否已到（毫秒计时回绕安全）
 *
 * @param now  当前系统时间（毫秒）
 * @param deadline  截止时间（毫秒）
 * @return 1=已到期，0=未到期
 */
static uint8_t deadline_expired(uint32_t now, uint32_t deadline)
{
    return ((int32_t)(now - deadline) >= 0);
}

/**
 * @brief  向上层回调上报协议事件
 *
 * @param event  事件类型（见 usbpd_protocol_event_e）
 * @param msg  事件关联报文（可为 NULL）
 */
static void protocol_emit(uint8_t event, const struct usbpd_protocol_msg_t *msg)
{
    if (g_protocol.handler != 0)
        g_protocol.handler(event, msg, g_protocol.handler_arg);
}

/**
 * @brief  上报本端发送帧已被 GoodCRC 确认事件（USBPD_PROTOCOL_TX_GOODCRC）
 *
 * 依据待确认帧缓存恢复报文头与负载内容，构造事件消息后回调上报。
 */
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

/**
 * @brief  构造 USB PD 报文头
 *
 * 填充类型、版本、MessageID、数据对象数与扩展标志；SOP 帧额外携带电源/数据角色。
 *
 * @param header  输出报文头
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param type  消息类型（见 usbpd_ctrl_e / usbpd_data_e / usbpd_extended_e）
 * @param objects  数据对象数量（0..7）
 * @param extended  扩展消息标志（1=扩展消息）
 */
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

/**
 * @brief  组帧并交由 PHY 层发送，随后进入等待 GoodCRC 状态
 *
 * 发送成功时缓存原始帧用于超时重发，并启动 GoodCRC 等待定时。
 *
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param header  已构造的报文头
 * @param payload  负载数据（无负载时可为 NULL）
 * @param bytes  负载字节数（0..28，须为 4 的倍数）
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 发起成功；USBPD_ERR 参数非法；USBPD_BUSY PHY 忙
 */
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

/**
 * @brief  发送扩展消息的当前分块（Chunked Extended Message）
 *
 * 按扩展消息头与剩余长度切分数据，单块最多 USBPD_EXT_CHUNK_DATA_MAX 字节，
 * 发送成功后推进发送偏移与分块序号。
 *
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 已发起发送；USBPD_BUSY 非发送时机或 PHY 忙
 */
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

/**
 * @brief  向对端发送 Chunk Request，请求扩展消息的下一分块
 *
 * 发送成功后进入等待数据块状态，并启动 tChunkSenderResponse 超时定时。
 *
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 已发起请求；USBPD_BUSY 非请求时机或 PHY 忙
 */
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

/**
 * @brief  处理接收到的扩展消息（分块重组与 Chunk Request 应答）
 *
 * 校验扩展消息头合法性；分块数据按分块序号写入接收重组缓冲；尚未收完时
 * 置位 Chunk Request 待发标志，全部收完后以 USBPD_PROTOCOL_EXT_RX 事件上报。
 *
 * @param frame  接收到的原始帧
 */
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

/**
 * @brief  协议层帧接收处理入口
 *
 * 处理 Hard Reset、GoodCRC 确认、MessageID 去重与版本协商、Soft Reset；
 * 扩展消息转入分块处理，其余普通报文以 USBPD_PROTOCOL_RX 事件上报。
 *
 * @param frame  接收到的原始帧
 */
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

/* ===== 对外接口 ===== */
/**
 * @brief  配置协议层版本与角色，并同步设置 PHY 层角色
 *
 * @param revision  协议版本（见 usbpd_revision_e，大于 Rev3.0 按 Rev3.0 处理）
 * @param power_role  电源角色（0=Sink，非 0=Source）
 * @param data_role  数据角色（0=UFP，非 0=DFP）
 */
void usbpd_protocol_configure(uint8_t revision, uint8_t power_role, uint8_t data_role)
{
    g_protocol.revision = (revision <= USBPD_REV30) ? revision : USBPD_REV30;
    g_protocol.power_role = (power_role != 0U) ? USBPD_POWER_ROLE_SOURCE : USBPD_POWER_ROLE_SINK;
    g_protocol.data_role = (data_role != 0U) ? USBPD_DATA_ROLE_DFP : USBPD_DATA_ROLE_UFP;
    usbpd_phy_set_roles(g_protocol.power_role, g_protocol.data_role);
}

/**
 * @brief  发送控制消息（无数据对象）
 *
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param type  控制消息类型（见 usbpd_ctrl_e）
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 发起成功；USBPD_ERR_PARAM 参数非法；USBPD_BUSY 上一帧尚未被确认
 */
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

/**
 * @brief  发送数据消息（携带数据对象）
 *
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param type  数据消息类型（见 usbpd_data_e）
 * @param objects  数据对象数组（小端序 32 位字）
 * @param count  数据对象数量（1..USBPD_MAX_DATA_OBJ）
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 发起成功；USBPD_ERR_PARAM 参数非法；USBPD_BUSY 上一帧尚未被确认
 */
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

/**
 * @brief  通过 SOP 通道发送扩展消息（自动分块）
 *
 * @param type  扩展消息类型（见 usbpd_extended_e）
 * @param data  扩展数据缓冲
 * @param length  数据长度（1..USBPD_EXT_DATA_MAX 字节）
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 首块发起成功；USBPD_ERR_PARAM 参数非法；USBPD_BUSY 发送未就绪
 */
int usbpd_protocol_send_extended(uint8_t type, const uint8_t *data, uint16_t length, uint32_t now_ms)
{
    return usbpd_protocol_send_extended_sop(USBPD_SOP, type, data, length, now_ms);
}

/**
 * @brief  通过指定 SOP 通道发送扩展消息（自动分块）
 *
 * 长度超过 USBPD_EXT_CHUNK_DATA_MAX 时自动分块，每块发出后等待对端
 * Chunk Request 再继续发送下一块。
 *
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param type  扩展消息类型（见 usbpd_extended_e）
 * @param data  扩展数据缓冲
 * @param length  数据长度（1..USBPD_EXT_DATA_MAX 字节）
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 首块发起成功；USBPD_ERR_PARAM 参数非法；USBPD_BUSY 发送未就绪
 */
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

/**
 * @brief  发送 Extended Control 消息（两字节扩展控制数据块）
 *
 * @param subtype  扩展控制类型（见 usbpd_extended_control_e）
 * @param data  与类型相关的数据字节
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 首块发起成功；USBPD_ERR_PARAM 参数非法；USBPD_BUSY 发送未就绪
 */
int usbpd_protocol_send_extended_control(uint8_t subtype, uint8_t data, uint32_t now_ms)
{
    const uint8_t ecdb[2] = {subtype, data};
    if ((subtype < USBPD_EXT_CTRL_EPR_GET_SOURCE_CAP) || (subtype > USBPD_EXT_CTRL_EPR_KEEPALIVE_ACK))
        return USBPD_ERR_PARAM;
    return usbpd_protocol_send_extended(USBPD_EXT_CONTROL, ecdb, sizeof(ecdb), now_ms);
}

/**
 * @brief  发送 Hard Reset 信号（经 PHY 层发送，不等 GoodCRC）
 *
 * 发送前放弃当前待确认帧。
 *
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 发起成功；USBPD_BUSY PHY 忙
 */
int usbpd_protocol_send_hard_reset(uint32_t now_ms)
{
    g_protocol.awaiting_crc = 0U;
    return usbpd_phy_send(USBPD_SOP_HARD_RESET, 0, 0U, now_ms);
}

/**
 * @brief  复位协议层运行状态
 *
 * 保留事件回调与版本/角色配置，清空 MessageID、收发缓冲与全部扩展消息状态。
 */
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

/**
 * @brief  初始化协议层，注册事件回调并复位全部状态
 *
 * 默认协议版本为 Rev3.0，角色需另行调用 usbpd_protocol_configure() 配置。
 *
 * @param handler  协议事件回调（事件类型见 usbpd_protocol_event_e）
 * @param arg  回调用户参数
 */
void usbpd_protocol_init(usbpd_protocol_handler_t handler, void *arg)
{
    memset(&g_protocol, 0, sizeof(g_protocol));
    g_protocol.handler = handler;
    g_protocol.handler_arg = arg;
    g_protocol.revision = USBPD_REV30;
}

/**
 * @brief  协议层周期任务（需在主循环中以毫秒节拍调用）
 *
 * 依次处理：接收溢出恢复、帧接收分发、GoodCRC 超时重发（超过
 * USBPD_N_RETRY_COUNT 上报 USBPD_PROTOCOL_TX_TIMEOUT）、扩展消息分块
 * 收发调度与分块超时处理。
 *
 * @param now_ms  当前系统时间（毫秒）
 */
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
