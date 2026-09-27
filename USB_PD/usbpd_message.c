#include "usbpd_message.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static struct message_buffer_t g_observer;

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

uint8_t usbpd_observer_pop(struct message_buffer_record_t *record)
{
    return message_buffer_pop(&g_observer, record);
}

void usbpd_observer_clear(void)
{
    message_buffer_clear(&g_observer);
}
uint16_t usbpd_observer_count(void)
{
    return message_buffer_count(&g_observer);
}
uint32_t usbpd_observer_dropped(void)
{
    return message_buffer_dropped(&g_observer);
}

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

void usbpd_observer_init(void)
{
    message_buffer_init(&g_observer);
}