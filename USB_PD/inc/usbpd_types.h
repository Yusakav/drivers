#ifndef USBPD_TYPES_H
#define USBPD_TYPES_H

#include <stdint.h>

#define USBPD_OK             0
#define USBPD_ERR           (-1)
#define USBPD_BUSY          (-2)
#define USBPD_MAX_DATA_OBJ    7U
#define USBPD_MAX_FRAME_LEN  30U
#define USBPD_TRACE_FRAME_LEN 34U
#define USBPD_EXT_DATA_MAX  260U

enum usbpd_revision_e {
    USBPD_REV20 = 1U,
    USBPD_REV30 = 2U,
};

enum usbpd_sop_e {
    USBPD_SOP = 0U,
    USBPD_SOP_PRIME,
    USBPD_SOP_DPRIME,
    USBPD_SOP_HARD_RESET,
    USBPD_SOP_CABLE_RESET,
    USBPD_SOP_INVALID = 7U,
};

enum usbpd_cc_e {
    USBPD_CC_OPEN = 0U,
    USBPD_CC_RA,
    USBPD_CC_RD,
    USBPD_CC_RP_DEF = 5U,
    USBPD_CC_RP_1A5,
    USBPD_CC_RP_3A,
};

enum usbpd_ctrl_e {
    USBPD_CTRL_GOODCRC = 1U,
    USBPD_CTRL_ACCEPT = 3U,
    USBPD_CTRL_REJECT = 4U,
    USBPD_CTRL_PS_RDY = 6U,
    USBPD_CTRL_GET_SOURCE_CAP = 7U,
    USBPD_CTRL_GET_SINK_CAP = 8U,
    USBPD_CTRL_WAIT = 12U,
    USBPD_CTRL_SOFT_RESET = 13U,
    USBPD_CTRL_NOT_SUPPORTED = 16U,
    USBPD_CTRL_GET_SOURCE_CAP_EXT = 17U,
    USBPD_CTRL_GET_STATUS = 18U,
    USBPD_CTRL_GET_PPS_STATUS = 20U,
    USBPD_CTRL_GET_SINK_CAP_EXT = 22U,
    USBPD_CTRL_GET_SOURCE_INFO = 23U,
    USBPD_CTRL_GET_REVISION = 24U,
};

enum usbpd_data_e {
    USBPD_DATA_SOURCE_CAP = 1U,
    USBPD_DATA_REQUEST = 2U,
    USBPD_DATA_SINK_CAP = 4U,
    USBPD_DATA_EPR_REQUEST = 9U,
    USBPD_DATA_EPR_MODE = 10U,
    USBPD_DATA_VENDOR_DEFINED = 15U,
};

enum usbpd_pdo_type_e {
    USBPD_PDO_FIXED = 0U,
    USBPD_PDO_BATTERY = 1U,
    USBPD_PDO_VARIABLE = 2U,
    USBPD_PDO_APDO = 3U,
};

enum usbpd_apdo_type_e {
    USBPD_APDO_PPS = 0U,
    USBPD_APDO_EPR_AVS = 1U,
};

union usbpd_header_u {
    uint16_t raw;
    uint8_t bytes[2];
    struct {
        uint16_t type : 5;
        uint16_t data_role : 1;
        uint16_t revision : 2;
        uint16_t power_role : 1;
        uint16_t message_id : 3;
        uint16_t data_objects : 3;
        uint16_t extended : 1;
    } bits;
};

union usbpd_ext_header_u {
    uint16_t raw;
    struct {
        uint16_t data_size : 9;
        uint16_t reserved : 1;
        uint16_t request_chunk : 1;
        uint16_t chunk_number : 4;
        uint16_t chunked : 1;
    } bits;
};

union usbpd_pdo_u {
    uint32_t raw;
    uint8_t bytes[4];
    struct {
        uint32_t current_10ma : 10;
        uint32_t voltage_50mv : 10;
        uint32_t peak_current : 2;
        uint32_t reserved0 : 1;
        uint32_t epr_capable : 1;
        uint32_t unchunked : 1;
        uint32_t drd : 1;
        uint32_t usb_comm : 1;
        uint32_t unconstrained : 1;
        uint32_t suspend : 1;
        uint32_t drp : 1;
        uint32_t type : 2;
    } fixed;
    struct {
        uint32_t current_50ma : 7;
        uint32_t reserved0 : 1;
        uint32_t min_voltage_100mv : 8;
        uint32_t reserved1 : 1;
        uint32_t max_voltage_100mv : 8;
        uint32_t reserved2 : 2;
        uint32_t power_limited : 1;
        uint32_t subtype : 2;
        uint32_t type : 2;
    } pps;
    struct {
        uint32_t pdp_w : 8;
        uint32_t min_voltage_100mv : 8;
        uint32_t reserved0 : 1;
        uint32_t max_voltage_100mv : 9;
        uint32_t peak_current : 2;
        uint32_t subtype : 2;
        uint32_t type : 2;
    } avs;
    struct {
        uint32_t fields : 30;
        uint32_t type : 2;
    } common;
};

union usbpd_rdo_u {
    uint32_t raw;
    uint8_t bytes[4];
    struct {
        uint32_t max_current_10ma : 10;
        uint32_t operating_current_10ma : 10;
        uint32_t reserved : 2;
        uint32_t epr_capable : 1;
        uint32_t unchunked : 1;
        uint32_t no_suspend : 1;
        uint32_t usb_comm : 1;
        uint32_t mismatch : 1;
        uint32_t give_back : 1;
        uint32_t object_position : 4;
    } fixed;
    struct {
        uint32_t operating_current_50ma : 7;
        uint32_t reserved0 : 2;
        uint32_t output_voltage_20mv : 12;
        uint32_t reserved1 : 1;
        uint32_t epr_capable : 1;
        uint32_t unchunked : 1;
        uint32_t no_suspend : 1;
        uint32_t usb_comm : 1;
        uint32_t mismatch : 1;
        uint32_t reserved2 : 1;
        uint32_t object_position : 4;
    } pps;
    struct {
        uint32_t operating_current_50ma : 7;
        uint32_t reserved0 : 2;
        uint32_t output_voltage_25mv : 12;
        uint32_t reserved1 : 1;
        uint32_t epr_capable : 1;
        uint32_t unchunked : 1;
        uint32_t no_suspend : 1;
        uint32_t usb_comm : 1;
        uint32_t mismatch : 1;
        uint32_t reserved2 : 1;
        uint32_t object_position : 4;
    } avs;
};

struct usbpd_frame_t {
    union usbpd_header_u header;
    uint8_t payload[USBPD_MAX_DATA_OBJ * 4U];
    uint8_t payload_len;
    uint8_t sop;
    uint8_t raw_len;
};

typedef char usbpd_header_size_must_be_2[(sizeof(union usbpd_header_u) == 2U) ? 1 : -1];
typedef char usbpd_pdo_size_must_be_4[(sizeof(union usbpd_pdo_u) == 4U) ? 1 : -1];
typedef char usbpd_rdo_size_must_be_4[(sizeof(union usbpd_rdo_u) == 4U) ? 1 : -1];

#endif
