/**
* @file usbpd_message.c
* @brief USB PD 报文观察器（Observer）实现
*
* 所属模块：USB PD 协议栈（调试辅助层，与具体控制器/平台无关）。
* 对应头文件：usbpd_message.h。
*
* 本文件实现：
*   - 单例环形缓冲 g_observer 的记录登记/弹出/清空/查询接口；
*   - 将记录解码为人类可读日志行的格式化逻辑（消息类型名、SOP、
*     MessageID、RDO/PDO 概要等），供 usbpd_observer_service() 输出。
*
* 注意：所有内部静态函数（observer_message_name / observer_sop_name /
* observer_append）均为解码辅助，不对外暴露。
*/
#include "usbpd_message.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ===== 内部状态 ===== */
static struct message_buffer_t g_observer;  /* 观察器全局环形缓冲单例 */

/* ===== 内部辅助函数 ===== */
/**
 * @brief  根据记录中的报文头返回可读的消息类型字符串
 *
 * 覆盖控制/数据/扩展三类消息的全部类型枚举；短记录或复位/错误标志
 * 记录分别返回 "RESET" / "ERROR"。
 *
 * @param record  观察器记录（见 message_buffer_record_t）
 * @return 静态只读字符串指针（不须释放）
 */
static const char *observer_message_name(const struct message_buffer_record_t *record)
{
    union usbpd_header_u header;
    if (record->length < 2U)
        return ((record->flags & USBPD_OBSERVER_ERROR) != 0U) ? "ERROR" : "RESET";
    header.bytes[0] = record->payload[0];
    header.bytes[1] = record->payload[1];
    if (header.bits.data_objects != 0U)
    {
        if (header.bits.extended != 0U)
        {
            switch (header.bits.type)
            {
            case USBPD_EXT_SOURCE_CAP:
                return "SOURCE_CAP_EXT";
            case USBPD_EXT_STATUS:
                return "STATUS";
            case USBPD_EXT_GET_BATTERY_CAP:
                return "GET_BATTERY_CAP";
            case USBPD_EXT_GET_BATTERY_STATUS:
                return "GET_BATTERY_STATUS";
            case USBPD_EXT_BATTERY_CAP:
                return "BATTERY_CAP";
            case USBPD_EXT_GET_MANUFACTURER_INFO:
                return "GET_MANUFACTURER_INFO";
            case USBPD_EXT_MANUFACTURER_INFO:
                return "MANUFACTURER_INFO";
            case USBPD_EXT_SECURITY_REQUEST:
                return "SECURITY_REQUEST";
            case USBPD_EXT_SECURITY_RESPONSE:
                return "SECURITY_RESPONSE";
            case USBPD_EXT_FIRMWARE_UPDATE_REQUEST:
                return "FW_UPDATE_REQUEST";
            case USBPD_EXT_FIRMWARE_UPDATE_RESPONSE:
                return "FW_UPDATE_RESPONSE";
            case USBPD_EXT_PPS_STATUS:
                return "PPS_STATUS";
            case USBPD_EXT_COUNTRY_INFO:
                return "COUNTRY_INFO";
            case USBPD_EXT_COUNTRY_CODES:
                return "COUNTRY_CODES";
            case USBPD_EXT_SINK_CAP:
                return "SINK_CAP_EXT";
            case USBPD_EXT_CONTROL:
                return "EXT_CONTROL";
            case USBPD_EXT_EPR_SOURCE_CAP:
                return "EPR_SOURCE_CAP";
            case USBPD_EXT_EPR_SINK_CAP:
                return "EPR_SINK_CAP";
            case USBPD_EXT_VENDOR_DEFINED:
                return "VDM_EXT";
            default:
                return "EXT_RESERVED";
            }
        }
        switch (header.bits.type)
        {
        case USBPD_DATA_SOURCE_CAP:
            return "SOURCE_CAP";
        case USBPD_DATA_REQUEST:
            return "REQUEST";
        case USBPD_DATA_BIST:
            return "BIST";
        case USBPD_DATA_SINK_CAP:
            return "SINK_CAP";
        case USBPD_DATA_BATTERY_STATUS:
            return "BATTERY_STATUS";
        case USBPD_DATA_ALERT:
            return "ALERT";
        case USBPD_DATA_GET_COUNTRY_INFO:
            return "GET_COUNTRY_INFO";
        case USBPD_DATA_ENTER_USB:
            return "ENTER_USB";
        case USBPD_DATA_EPR_REQUEST:
            return "EPR_REQUEST";
        case USBPD_DATA_EPR_MODE:
            return "EPR_MODE";
        case USBPD_DATA_SOURCE_INFO:
            return "SOURCE_INFO";
        case USBPD_DATA_REVISION:
            return "REVISION";
        case USBPD_DATA_VENDOR_DEFINED:
            return "VDM";
        default:
            return "DATA_RESERVED";
        }
    }
    switch (header.bits.type)
    {
    case USBPD_CTRL_GOODCRC:
        return "GOODCRC";
    case USBPD_CTRL_GOTO_MIN:
        return "GOTO_MIN(DEPRECATED)";
    case USBPD_CTRL_ACCEPT:
        return "ACCEPT";
    case USBPD_CTRL_REJECT:
        return "REJECT";
    case USBPD_CTRL_PING:
        return "PING(DEPRECATED)";
    case USBPD_CTRL_PS_RDY:
        return "PS_RDY";
    case USBPD_CTRL_GET_SOURCE_CAP:
        return "GET_SOURCE_CAP";
    case USBPD_CTRL_GET_SINK_CAP:
        return "GET_SINK_CAP";
    case USBPD_CTRL_DR_SWAP:
        return "DR_SWAP";
    case USBPD_CTRL_PR_SWAP:
        return "PR_SWAP";
    case USBPD_CTRL_VCONN_SWAP:
        return "VCONN_SWAP";
    case USBPD_CTRL_WAIT:
        return "WAIT";
    case USBPD_CTRL_SOFT_RESET:
        return "SOFT_RESET";
    case USBPD_CTRL_DATA_RESET:
        return "DATA_RESET";
    case USBPD_CTRL_DATA_RESET_COMPLETE:
        return "DATA_RESET_COMPLETE";
    case USBPD_CTRL_NOT_SUPPORTED:
        return "NOT_SUPPORTED";
    case USBPD_CTRL_GET_SOURCE_CAP_EXT:
        return "GET_SOURCE_CAP_EXT";
    case USBPD_CTRL_GET_STATUS:
        return "GET_STATUS";
    case USBPD_CTRL_FR_SWAP:
        return "FR_SWAP";
    case USBPD_CTRL_GET_PPS_STATUS:
        return "GET_PPS_STATUS";
    case USBPD_CTRL_GET_COUNTRY_CODES:
        return "GET_COUNTRY_CODES";
    case USBPD_CTRL_GET_SINK_CAP_EXT:
        return "GET_SINK_CAP_EXT";
    case USBPD_CTRL_GET_SOURCE_INFO:
        return "GET_SOURCE_INFO";
    case USBPD_CTRL_GET_REVISION:
        return "GET_REVISION";
    default:
        return "CONTROL_RESERVED";
    }
}

/**
 * @brief  将 SOP 类型映射为可读字符串
 *
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @return 静态只读字符串指针（不须释放）
 */
static const char *observer_sop_name(uint8_t sop)
{
    switch (sop)
    {
    case USBPD_SOP:
        return "SOP";
    case USBPD_SOP_PRIME:
        return "SOP'";
    case USBPD_SOP_DPRIME:
        return "SOP''";
    case USBPD_SOP_HARD_RESET:
        return "HARD_RESET";
    case USBPD_SOP_CABLE_RESET:
        return "CABLE_RESET";
    default:
        return "INVALID";
    }
}

/* ===== 对外接口 ===== */
/**
 * @brief  向观察器登记一条原始帧记录（自动存入环形缓冲）
 *
 * 长度超过 USBPD_MESSAGE_BUFFER_PAYLOAD_MAX 时被截断并附加 USBPD_OBSERVER_ERROR 标志。
 *
 * @param direction  传输方向（MESSAGE_BUFFER_TX=发送，MESSAGE_BUFFER_RX=接收）
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param raw  原始帧字节（可为 NULL，但 length 须为 0）
 * @param length  原始帧长度（字节）
 * @param timestamp_ms  记录时刻（毫秒）
 * @param flags  标志位（见 usbpd_observer_flag_e，可按位 OR 组合）
 */
void usbpd_observer_record(uint8_t direction, uint8_t sop, const uint8_t *raw, uint8_t length, uint32_t timestamp_ms,
                           uint8_t flags)
{
    struct message_buffer_record_t record;
    if ((raw == 0) && (length != 0U))
        return;
    memset(&record, 0, sizeof(record));
    if (length > USBPD_MESSAGE_BUFFER_PAYLOAD_MAX)
    {
        length = USBPD_MESSAGE_BUFFER_PAYLOAD_MAX;
        flags |= USBPD_OBSERVER_ERROR;
    }
    record.timestamp_ms = timestamp_ms;
    record.direction = direction;
    record.channel = sop;
    record.flags = flags;
    record.length = length;
    if (length != 0U)
        memcpy(record.payload, raw, length);
    (void)message_buffer_push(&g_observer, &record);
}

/**
 * @brief  从观察器环形缓冲中弹出一条记录
 *
 * @param record  输出记录结构
 * @return 1=成功弹出，0=缓冲为空
 */
uint8_t usbpd_observer_pop(struct message_buffer_record_t *record)
{
    return message_buffer_pop(&g_observer, record);
}

/** @brief  清空观察器环形缓冲 */
void usbpd_observer_clear(void)
{
    message_buffer_clear(&g_observer);
}

/**
 * @brief  查询观察器当前待处理记录数
 * @return 待处理记录数
 */
uint16_t usbpd_observer_count(void)
{
    return message_buffer_count(&g_observer);
}

/**
 * @brief  查询观察器自初始化以来累计丢弃记录数
 * @return 丢弃记录总数
 */
uint32_t usbpd_observer_dropped(void)
{
    return message_buffer_dropped(&g_observer);
}

/**
 * @brief  向日志行追加格式化文本（vsnprintf 封装，容量安全）
 *
 * 当追加结果超出剩余容量时，截断至 capacity-1 字节；对 NULL 指针或容量已满直接返回。
 *
 * @param line  目标日志行缓冲
 * @param capacity  缓冲总容量（字节）
 * @param used  已用长度（读写指针）
 * @param format  printf 格式串
 */
static void observer_append(char *line, size_t capacity, size_t *used, const char *format, ...)
{
    int result;
    va_list args;
    if ((line == 0) || (used == 0) || (*used >= capacity))
        return;
    va_start(args, format);
    result = vsnprintf(&line[*used], capacity - *used, format, args);
    va_end(args);
    if (result < 0)
        return;
    if ((size_t)result >= (capacity - *used))
        *used = capacity - 1U;
    else
        *used += (size_t)result;
}

/**
 * @brief  格式化并刷写观察器记录到回调输出
 *
 * 每条记录依次输出：时间戳 / TX·RX / SOP 名 / 消息类型名；
 * 随后根据消息类型解码 RDO 对象位置、扩展消息头、Source_Cap / Sink_Cap
 * 中各 PDO 的电压电流概要；最后追加原始字节十六进制串。
 *
 * @param max_records  本次最多处理记录数
 * @param write  输出回调（可为 NULL，此时仅消费记录不输出）
 * @param arg  回调用户参数
 */
void usbpd_observer_service(uint8_t max_records, usbpd_observer_write_fn write, void *arg)
{
    struct message_buffer_record_t record;
    uint8_t index;
    while ((max_records-- != 0U) && (usbpd_observer_pop(&record) != 0U))
    {
        char line[320];
        size_t used = 0U;
        observer_append(line, sizeof(line), &used, "[USBPD][%lu][%s][%s][%s]", (unsigned long)record.timestamp_ms,
                        record.direction == MESSAGE_BUFFER_TX ? "TX" : "RX", observer_sop_name(record.channel),
                        observer_message_name(&record));
        if ((record.length >= 2U) && (used != 0U))
        {
            union usbpd_header_u header;
            header.bytes[0] = record.payload[0];
            header.bytes[1] = record.payload[1];
            observer_append(line, sizeof(line), &used, " id=%u ndo=%u rev=%u", header.bits.message_id,
                            header.bits.data_objects, header.bits.revision);
            if ((header.bits.extended != 0U) && (record.length >= 4U))
            {
                union usbpd_ext_header_u ext;
                ext.bytes[0] = record.payload[2];
                ext.bytes[1] = record.payload[3];
                observer_append(line, sizeof(line), &used, " size=%u chunk=%u%s", ext.bits.data_size,
                                ext.bits.chunk_number, ext.bits.request_chunk ? " request" : "");
            }
            else if ((header.bits.type == USBPD_DATA_REQUEST || header.bits.type == USBPD_DATA_EPR_REQUEST) &&
                     (header.bits.data_objects == 1U) && (record.length >= 6U))
            {
                union usbpd_rdo_u rdo;
                memcpy(rdo.bytes, &record.payload[2], sizeof(rdo.bytes));
                observer_append(line, sizeof(line), &used, " pdo=%u raw_rdo=%08lX", rdo.fixed.object_position,
                                (unsigned long)rdo.raw);
            }
            else if (((header.bits.type == USBPD_DATA_SOURCE_CAP) || (header.bits.type == USBPD_DATA_SINK_CAP)) &&
                     (header.bits.extended == 0U))
            {
                uint8_t object;
                for (object = 0U; object < header.bits.data_objects; ++object)
                {
                    union usbpd_pdo_u pdo;
                    size_t offset = (size_t)2U + (size_t)object * 4U;
                    if ((offset + 4U > record.length) || ((size_t)used >= sizeof(line) - 48U))
                        break;
                    memcpy(pdo.bytes, &record.payload[offset], sizeof(pdo.bytes));
                    if (pdo.common.type == USBPD_PDO_FIXED)
                    {
                        observer_append(line, sizeof(line), &used, " pdo%u=fixed:%lumV/%lumA", object + 1U,
                                        (unsigned long)pdo.fixed.voltage_50mv * 50UL,
                                        (unsigned long)pdo.fixed.current_10ma * 10UL);
                    }
                    else if (pdo.common.type == USBPD_PDO_BATTERY)
                    {
                        observer_append(line, sizeof(line), &used, " pdo%u=battery:%lumW", object + 1U,
                                        (unsigned long)pdo.battery.power_250mw * 250UL);
                    }
                    else if (pdo.common.type == USBPD_PDO_VARIABLE)
                    {
                        observer_append(line, sizeof(line), &used, " pdo%u=variable:%lumA", object + 1U,
                                        (unsigned long)pdo.variable.current_10ma * 10UL);
                    }
                    else
                    {
                        observer_append(line, sizeof(line), &used, " pdo%u=apdo:%u", object + 1U, pdo.common.subtype);
                    }
                }
            }
        }
        for (index = 0U; (index < record.length) && (used != 0U) && (used < sizeof(line) - 4U); ++index)
        {
            observer_append(line, sizeof(line), &used, " %02X", record.payload[index]);
        }
        if (write != 0)
            write(line, arg);
    }
}

/** @brief  初始化观察器环形缓冲（底层调用 message_buffer_init） */
void usbpd_observer_init(void)
{
    message_buffer_init(&g_observer);
}
