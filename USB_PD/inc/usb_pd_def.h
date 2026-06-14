/**
 * @file pd_type.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-12-08
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef USB_PD_DEF_H
#define USB_PD_DEF_H

#include <stdint.h>
#include "usb_pd_opt.h"

/**
 * @brief Type-C 端口类型（Port Type）
 */
enum usbpd_typec_port_type_t {
    USBPD_TYPEC_PORT_SRC,  // Source（供电端）——提供电力
    USBPD_TYPEC_PORT_SNK,  // Sink（受电端）——消耗电力
    USBPD_TYPEC_PORT_DRP   // DRP（Dual Role Port）——可 Source/Sink 自动切换
};

/**
 * @brief PD 协议版本（PD Revision）
 */
enum usbpd_revision_t {
    USBPD_SPECIFICATION_REV1 = 0b00,      // Revision 1.0 (Deprecated)
    USBPD_SPECIFICATION_REV2 = 0b01,      // Revision 2.0
    USBPD_SPECIFICATION_REV3 = 0b10,      // Revision 3.x
    USBPD_SPECIFICATION_RESERVED = 0b11,  // Reserved, Shall Not be used
};

/**
 * @brief 数据角色（Data Role）
 */
enum usbpd_data_role_type_e {
    USBPD_DATA_ROLE_UFP = 0b0,  // UFP：设备（如手机）
    USBPD_DATA_ROLE_DFP = 0b1,  // DFP：主机（如电脑）
};

/**
 * @brief 电源角色（Power Role）
 */
enum usbpd_power_role_type_e {
    USBPD_POWER_ROLE_SINK = 0b0,    // Sink（受电端）
    USBPD_POWER_ROLE_SOURCE = 0b1,  // Source（供电端）
};

/**
 * @brief USB PD Power Data Object 类型
 */
enum usbpd_pdo_type_e {
    USBPD_PDO_TYPE_FIXED = 0b00,     // 固定电压输出：Vmin = Vmax
    USBPD_PDO_TYPE_BATTERY = 0b01,   // 电池电源输出
    USBPD_PDO_TYPE_VARIABLE = 0b10,  // 可变电压输出（非电池）
    USBPD_PDO_TYPE_APDO = 0b11,      // 增强型功率输出（APDO/可编程电源）
};


/**
 * @brief 增强型电力数据对象（APDO）子类型
 * Bits [29:28]
 */
enum usbpd_apdo_type_e {
    USBPD_APDO_SPR_PPS = 0b00,  // SPR PPS（可编程电源，≤100W）
    USBPD_APDO_EPR_AVS = 0b01,  // EPR AVS（可调电压，≤240W）
    USBPD_APDO_SPR_AVS = 0b10,  // SPR AVS（SPR 范围的 AVS）
    USBPD_APDO_RESERVED = 0b11  // 保留
};



/**
 * @brief USB PD 控制消息类型（Control Message Types）
 *
 * 控制消息不携带 Data Objects
 * 编码来自 USB PD 规范 MessageType 字段（5-bit）
 */
enum usbpd_ctrl_message_type_e {
    USBPD_CTRL_MSG_RESERVED = 0x00u,                 // 0b00000：保留（不使用）
    USBPD_CTRL_MSG_GOOD_CRC = 0x01u,                 // 0b00001：GoodCRC —— 用来确认对方发送的消息, 限定时间内
    USBPD_CTRL_MSG_GO_TO_MIN = 0x02u,                // 0b00010：GotoMin —— 请求降至最小电压，适用于
    USBPD_CTRL_MSG_ACCEPT = 0x03u,                   // 0b00011：Accept —— 同意前一条消息, 限定时间内
    USBPD_CTRL_MSG_REJECT = 0x04u,                   // 0b00100：Reject —— 拒绝前一条消息, 限定时间内
    USBPD_CTRL_MSG_PING = 0x05u,                     // 0b00101：Ping —— 链路心跳
    USBPD_CTRL_MSG_PS_READY = 0x06u,                 // 0b00110：PS_RDY —— 电源准备完成
    USBPD_CTRL_MSG_GET_SOURCE_CAP = 0x07u,           // 0b00111：请求 Source Capabilities
    USBPD_CTRL_MSG_GET_SINK_CAP = 0x08u,             // 0b01000：请求 Sink Capabilities
    USBPD_CTRL_MSG_DR_SWAP = 0x09u,                  // 0b01001：数据角色交换（DFP/UFP）
    USBPD_CTRL_MSG_PR_SWAP = 0x0Au,                  // 0b01010：电源角色交换（SRC/SNK）
    USBPD_CTRL_MSG_VCONN_SWAP = 0x0Bu,               // 0b01011：VCONN 交换
    USBPD_CTRL_MSG_WAIT = 0x0Cu,                     // 0b01100：Wait —— 需要更多时间处理
    USBPD_CTRL_MSG_SOFT_RESET = 0x0Du,               // 0b01101：Soft Reset（软件重置）
    USBPD_CTRL_MSG_DATA_RESET = 0x0Eu,               // 0b01110：数据路径重置
    USBPD_CTRL_MSG_DATA_RESET_COMPLETE = 0x0Fu,      // 0b01111：数据路径重置完成
    USBPD_CTRL_MSG_NOT_SUPPORTED = 0x10u,            // 0b10000：不支持消息
    USBPD_CTRL_MSG_GET_SOURCE_CAP_EXTENDED = 0x11u,  // 0b10001：请求扩展源能力
    USBPD_CTRL_MSG_GET_STATUS = 0x12u,               // 0b10010：请求状态信息
    USBPD_CTRL_MSG_FR_SWAP = 0x13u,                  // 0b10011：快速角色切换（Fast Role Swap）
    USBPD_CTRL_MSG_GET_PPS_STATUS = 0x14u,           // 0b10100：请求 PPS 状态
    USBPD_CTRL_MSG_GET_COUNTRY_CODES = 0x15u,        // 0b10101：请求国家代码
    USBPD_CTRL_MSG_GET_SINK_CAP_EXTENDED = 0x16u,    // 0b10110：请求扩展 Sink 能力
    USBPD_CTRL_MSG_GET_SOURCE_INFO = 0x17u,          // 0b10111：请求 Source 信息
    USBPD_CTRL_MSG_REVISION = 0x18u,                 // 0b11000：请求 USB PD 协议版本
};

/**
 * @brief USB PD 数据消息类型（Data Message Types）
 *
 * 数据消息携带 1~7 个 Data Objects
 */
enum usbpd_data_message_type_e {
    USBPD_DATA_MSG_RESERVED = 0x00u,          // 0b00000：保留
    USBPD_DATA_MSG_SOURCE_CAP = 0x01u,        // 0b00001：Source Capabilities（供电能力列表，包含多个 PDO）
    USBPD_DATA_MSG_REQUEST = 0x02u,           // 0b00010：Request —— 请求某个 PDO
    USBPD_DATA_MSG_BIST = 0x03u,              // 0b00011：BIST 测试模式
    USBPD_DATA_MSG_SINK_CAP = 0x04u,          // 0b00100：Sink Capabilities（受电端能力）
    USBPD_DATA_MSG_BATTERY_STATUS = 0x05u,    // 0b00101：电池状态
    USBPD_DATA_MSG_ALERT = 0x06u,             // 0b00110：告警信息
    USBPD_DATA_MSG_GET_COUNTRY_INFO = 0x07u,  // 0b00111：获取国家/地区信息
    USBPD_DATA_MSG_ENTER_USB = 0x08u,         // 0b01000：进入 USB 模式（USB4/Alt Mode）
    USBPD_DATA_MSG_EPR_REQUEST = 0x09u,       // 0b01001：EPR 扩展功率范围请求
    USBPD_DATA_MSG_EPR_MODE = 0x0Au,          // 0b01010：EPR 模式
    USBPD_DATA_MSG_SRC_INFO = 0x0Bu,          // 0b01011：源端信息
    USBPD_DATA_MSG_REVISION = 0x0Cu,          // 0b01100：PD 规范版本信息
    USBPD_DATA_MSG_RESERVED_13 = 0x0Du,       // 0b01101：保留
    USBPD_DATA_MSG_RESERVED_14 = 0x0Eu,       // 0b01110：保留
    USBPD_DATA_MSG_VENDOR_DEFINED = 0x0Fu,    // 0b01111：VDM —— 厂商自定义数据
};

/**
 * @brief USB PD 扩展消息类型（Extended Message Types）
 *
 * 扩展消息携带 Variable-length Data Block（带 Extended Header）
 * ExtMsgType 字段占 5-bit（与 MessageType 独立）
 */
enum usbpd_extended_message_type_e {
    USBPD_EXTENDED_MSG_RESERVED = 0x00u,                  // 0b00000：保留
    USBPD_EXTENDED_MSG_SOURCE_CAP_EXTENDED = 0x01u,       // 0b00001：扩展 Source Capabilities
    USBPD_EXTENDED_MSG_STATUS = 0x02u,                    // 0b00010：设备状态（Status）
    USBPD_EXTENDED_MSG_GET_BATTERY_CAP = 0x03u,           // 0b00011：请求电池容量信息
    USBPD_EXTENDED_MSG_GET_BATTERY_STATUS = 0x04u,        // 0b00100：请求电池状态
    USBPD_EXTENDED_MSG_BATTERY_CAP = 0x05u,               // 0b00101：电池容量信息（Battery Capabilities）
    USBPD_EXTENDED_MSG_GET_MANUFACTURER_INFO = 0x06u,     // 0b00110：请求制造商信息
    USBPD_EXTENDED_MSG_MANUFACTURER_INFO = 0x07u,         // 0b00111：制造商信息响应
    USBPD_EXTENDED_MSG_SECURITY_REQUEST = 0x08u,          // 0b01000：安全请求（Authentication）
    USBPD_EXTENDED_MSG_SECURITY_RESPONSE = 0x09u,         // 0b01001：安全响应
    USBPD_EXTENDED_MSG_FIRMWARE_UPDATE_REQUEST = 0x0Au,   // 0b01010：固件更新请求（FW Update）
    USBPD_EXTENDED_MSG_FIRMWARE_UPDATE_RESPONSE = 0x0Bu,  // 0b01011：固件更新响应
    USBPD_EXTENDED_MSG_PPS_STATUS = 0x0Cu,                // 0b01100：PPS 状态（增强 PPS 反馈）
    USBPD_EXTENDED_MSG_COUNTRY_INFO = 0x0Du,              // 0b01101：国家/地区信息
    USBPD_EXTENDED_MSG_COUNTRY_CODES = 0x0Eu,             // 0b01110：国家代码列表
    USBPD_EXTENDED_MSG_SINK_CAP_EXTENDED = 0x0Fu,         // 0b01111：扩展 Sink Capabilities
    USBPD_EXTENDED_MSG_EXTENDED_CONTROL = 0x10u,          // 0b10000：Extended Control message（工程模式/厂商测试）
    USBPD_EXTENDED_MSG_EPR_SOURCE_CAP = 0x11u,            // 0b10001：EPR 专用 Source Cap（超高功率）
    USBPD_EXTENDED_MSG_EPR_SINK_CAP = 0x12u,              // 0b10010：EPR 专用 Sink Cap
    USBPD_EXTENDED_MSG_EPR_MODE_CAP = 0x13u,              // 0b10011：EPR 模式能力
    USBPD_EXTENDED_MSG_VENDOR_DEFINED = 0x1Fu,            // 0b11111：VDM —— 厂商自定义数据
};

// 端口状态机状态
/**
 * @brief USB PD 消息头（PD Header）
 */
union usbpd_message_header_u {
    uint16_t d16;   // 原始 16-bit 数据
    uint8_t d8[2];  // 原始 8-bit 数据

    struct
    {
        uint16_t msg_type : 5u;    // [4:0]   消息类型
        uint16_t data_role : 1u;   // [5]     数据角色：0=UFP,1=DFP @usbpd_data_role_t
        uint16_t specs_rev : 2u;   // [7:6]   协议版本 @usbpd_revision_t
        uint16_t power_role : 1u;  // [8]     电源角色：0=Sink,1=Source @usbpd_power_role_t
        uint16_t msg_id : 3u;      // [11:9]  消息 ID
        uint16_t n_data_obj : 3u;  // [14:12] 数据对象数量
        uint16_t extended : 1u;    // [15]    扩展消息标志
    } msg_header;
};

/**
 * @brief USB PD 扩展消息头（Extended Message Header）
 *
 * 用于分块或非分块扩展消息，最大支持 512 字节数据
 */
union usbpd_extended_message_header_u {
    uint16_t d16;   // 原始 16-bit 数据
    uint8_t d8[2];  // 原始 8-bit 数据

    struct
    {
        uint16_t data_size : 9;      // [8:0]   数据长度（字节），最大 512B
        uint16_t reserved : 1;       // [9]     保留，必须置 0
        uint16_t request_chunk : 1;  // [10]    请求块标志，1 表示请求下一块数据
        uint16_t chunk_number : 4;   // [14:11] 当前块编号，范围 0~15
        uint16_t chunked : 1;        // [15]    分块标志，1 = 分块消息，0 = 非分块消息
    } extended_msg_header;
};

/**
 * @brief USB PD PDO（Power Data Object）
 */
union usbpd_pdo_u {
    uint32_t d32;     // 原始 32-bit 数据
    uint16_t d16[2];  // 原始 16-bit 数据
    uint8_t d8[4];    // 原始 8-bit 数据

    /**
     * @brief 通用 PDO 结构（用于解析 PDO 类型）
     */
    struct
    {
        uint32_t specific_fields : 28u;  // [27:0] 具体字段，根据 PDO 类型而定
        uint32_t apdo_subtype : 2u;      // [29:28] APDO 子类型 (仅 B31-B30 = 0b11 时有效)
        uint32_t pdo_type : 2u;          // [31:30] PDO 类型 @usbpd_pdo_type
    } general;

    /**
     * @brief 固定电压PDO（Sink端）
     */
    struct
    {
        uint32_t max_current : 10;         // [9:0] 最大电流（单位：10mA）
        uint32_t voltage : 10;             // [19:10] 电压（单位：50mV）
        uint32_t peak_current : 2;         // [21:20] 峰值电流能力
        uint32_t : 1;                      // [22] 保留
        uint32_t epr_mode_capable : 1;     // [23] EPR模式能力
        uint32_t unchunked_supported : 1;  // [24] 支持非分块扩展消息
        uint32_t drd : 1;                  // [25] 双角色数据设备
        uint32_t usb_comm_capable : 1;     // [26] USB通信能力
        uint32_t unconstrained_pwr : 1;    // [27] 无约束功率
        uint32_t usb_suspend_sup : 1;      // [28] USB挂起支持
        uint32_t drp : 1;                  // [29] 双角色电源设备
        uint32_t type : 2;                 // [31:30] 类型（0 = Fixed）
    } fixed;


    /**
     * @brief 可变电压 PDO
     */
    struct
    {
        uint32_t current_max_10ma : 10u;  // [9:0]   最大电流 10mA
        uint32_t voltage_min_50mv : 10u;  // [19:10] 最小电压 50mV
        uint32_t voltage_max_50mv : 10u;  // [29:20] 最大电压 50mV
        uint32_t type : 2u;               // [31:30] PDO 类型 = Variable @usbpd_pdo_type_e
    } variable;

    /**
     * @brief 电池 PDO
     */
    struct
    {
        uint32_t power_max_250mw : 10u;   // [9:0]   最大功率 250mW
        uint32_t voltage_min_50mv : 10u;  // [19:10] 最小电压 50mV
        uint32_t voltage_max_50mv : 10u;  // [29:20] 最大电压 50mV
        uint32_t type : 2u;               // [31:30] PDO 类型 = Battery @usbpd_pdo_type_e
    } battery;

    /**
     * @brief 增强型 PDO（APDO / PPS）
     */
    struct
    {
        uint32_t current_max_50ma : 7u;   // [6:0]   最大电流 50mA
        uint32_t reserved1 : 1u;          // [7]     保留
        uint32_t voltage_min_100mv : 8u;  // [15:8]  最小电压 100mV
        uint32_t reserved2 : 1u;          // [16]    保留
        uint32_t voltage_max_100mv : 8u;  // [24:17] 最大电压 100mV
        uint32_t reserved3 : 2u;          // [26:25] 保留
        uint32_t pps_power_limited : 1u;  // [27]    PPS 限功标志
        uint32_t spr_programmable : 2u;   // [29:28] SPR 可编程电源 @usbpd_apdo_type_e*/
        uint32_t type : 2u;               // [31:30] PDO 类型 = APDO @usbpd_pdo_type_e
    } apdo;

    /**
     * @brief SPR Adjustable Voltage Supply APDO – Sink
     * @note 电压步进 25mV，最大电流根据规格不同
     */
    struct
    {
        uint32_t max_current_for_20v_10ma : 10u;  // [9:0]   最大电流 10mA (15V–20V)
        uint32_t max_current_for_15v_10ma : 10u;  // [19:10] 最大电流 10mA (9V–15V)
        uint32_t reserved_20_25 : 6u;             // [25:20] 保留
        uint32_t peak_current : 2u;               // [27:26] 峰值电流
        uint32_t spr_avs : 2u;                    // [29:28] SPR AVS 标识 @usbpd_apdo_type_e
        uint32_t apdo : 2u;                       // [31:30] PDO 类型 = APDO
    } spr_avs;

    /**
     * @brief EPR Adjustable Voltage Supply APDO – Sink
     * @note 电压步进 100mV，PDP 单位 1W
     */
    struct
    {
        uint32_t pdp_in_1w_units : 8u;        // [7:0]   PDP 单位 1W
        uint32_t minimum_voltage_100mv : 8u;  // [15:8]  最小电压 100mV
        uint32_t reserved_16 : 1u;            // [16]    保留
        uint32_t maximum_voltage_100mv : 9u;  // [25:17] 最大电压 100mV
        uint32_t peak_current : 2u;           // [27:26] 峰值电流
        uint32_t epr_avs : 2u;                // [29:28] EPR AVS 标识 @usbpd_apdo_type_e
        uint32_t apdo : 2u;                   // [31:30] PDO 类型 = APDO
    } epr_avs;
};

/**
 * @brief USB PD 请求数据对象（RDO）
 *
 * 可用于固定/可变 PDO、Battery PDO、PPS、AVS 请求
 */
union usbpd_rdo_u {
    uint32_t d32;     // 原始 32-bit 数据
    uint16_t d16[2];  // 原始 16-bit 数据
    uint8_t d8[4];    // 原始 8-bit 数据

    /**
     * @brief 固定/可变 PDO 请求对象（RDO）
     */
    struct
    {
        uint32_t current_extremum_10ma : 10u;     // [9:0]   最大电流 10mA
        uint32_t current_operate_10ma : 10u;      // [19:10] 当前电流 10mA
        uint32_t reserved : 2u;                   // [21:20] 保留
        uint32_t epr_mode_capable : 1u;           // [22]    支持 EPR
        uint32_t unchunked_ext_msg_support : 1u;  // [23]    支持未分块扩展消息
        uint32_t no_usb_suspend : 1u;             // [24]    无 USB 挂起
        uint32_t usb_comm_capable : 1u;           // [25]    支持 USB 通信
        uint32_t capability_mismatch : 1u;        // [26]    能力不匹配
        uint32_t give_back_flag : 1u;             // [27]    GiveBack 标志
        uint32_t object_position : 4u;            // [31:28] PDO 位置
    } fixed_variable;

    /**
     * @brief 电池 PDO 请求对象（Battery RDO）
     */
    struct
    {
        uint32_t power_extremum_250mw : 10u;      // [9:0]   最大功率 250mW
        uint32_t power_operate_250mw : 10u;       // [19:10] 工作功率 250mW
        uint32_t reserved : 2u;                   // [21:20] 保留
        uint32_t epr_mode_capable : 1u;           // [22]    支持 EPR
        uint32_t unchunked_ext_msg_support : 1u;  // [23]    支持未分块扩展消息
        uint32_t no_usb_suspend : 1u;             // [24]    无 USB 挂起
        uint32_t usb_comm_capable : 1u;           // [25]    支持 USB 通信
        uint32_t capability_mismatch : 1u;        // [26]    能力不匹配
        uint32_t give_back_flag : 1u;             // [27]    保留
        uint32_t object_position : 4u;            // [31:28] PDO 位置
    } battery;

    /**
     * @brief PPS 请求数据对象（Programmable Power Supply RDO）
     */
    struct
    {
        uint32_t operating_current_50ma : 7u;  // [6:0]   工作电流 50mA
        uint32_t reserved_7_8 : 2u;            // [8:7]   保留
        uint32_t output_voltage_20mv : 12u;    // [20:9]  输出电压 20mV
        uint32_t reserved_21 : 1u;             // [21]    保留
        uint32_t epr_capable : 1u;             // [22]    支持 EPR
        uint32_t unchunked_ext_msg_supp : 1u;  // [23]    支持未分块扩展消息
        uint32_t no_usb_suspend : 1u;          // [24]    无 USB 挂起
        uint32_t usb_comm_capable : 1u;        // [25]    USB 通信能力
        uint32_t capability_mismatch : 1u;     // [26]    能力不匹配
        uint32_t reserved_27 : 1u;             // [27]    保留
        uint32_t object_position : 4u;         // [31:28] PDO 位置号
    } pps;

    /**
     * @brief AVS 请求数据对象（Adjustable Voltage Supply RDO）
     */
    struct
    {
        uint32_t operating_current_50ma : 7u;  // [6:0]   工作电流 50mA
        uint32_t reserved_7_8 : 2u;            // [8:7]   保留
        uint32_t output_voltage_25mv : 12u;    // [20:9]  输出电压 25mV
        uint32_t reserved_21 : 1u;             // [21]    保留
        uint32_t epr_capable : 1u;             // [22]    请求 EPR 功能
        uint32_t unchunked_ext_msg_supp : 1u;  // [23]    支持未分块扩展消息
        uint32_t no_usb_suspend : 1u;          // [24]    不允许 USB Suspend
        uint32_t usb_comm_capable : 1u;        // [25]    USB 通信能力
        uint32_t capability_mismatch : 1u;     // [26]    能力不匹配
        uint32_t reserved_27 : 1u;             // [27]    保留
        uint32_t object_position : 4u;         // [31:28] PDO 位置号
    } avs;
};

/**
 * @brief USB PD CC 引脚瞬时电压状态枚举
 */
enum usbpd_cc_voltage_status_e {
    TYPEC_CC_VOLT_OPEN = 0b000,    // 悬空 (Open)
    TYPEC_CC_VOLT_RA = 0b001,      // 检测到 Ra (Source 视角)
    TYPEC_CC_VOLT_RD = 0b010,      // 检测到 Rd (Source 视角)
    TYPEC_CC_VOLT_RP_DEF = 0b101,  // 对方为 Rp-Default (500/900mA)
    TYPEC_CC_VOLT_RP_1_5 = 0b110,  // 对方为 Rp-1.5A
    TYPEC_CC_VOLT_RP_3_0 = 0b111,  // 对方为 Rp-3.0A
};

/**
 * @brief USB PD CC 引脚端接(电阻)配置枚举
 */
enum usbpd_cc_pull_config_e {
    TYPEC_CC_PULL_RA = 0b00,    // 挂载 Ra 电阻 (音频/电缆)
    TYPEC_CC_PULL_RP = 0b01,    // 挂载 Rp 电阻 (作为 Source)
    TYPEC_CC_PULL_RD = 0b10,    // 挂载 Rd 电阻 (作为 Sink)
    TYPEC_CC_PULL_OPEN = 0b11,  // 无端接 (断开)
};

/**
 * @brief USB PD CC 逻辑连接状态枚举
 */
enum usbpd_cc_state_e {
    USBPD_CC_STATE_UNATTACHED = 0b000,    // 未附着 (空闲/轮询)
    USBPD_CC_STATE_ATTACHED_UFP = 0b001,  // 已附着为 Sink (UFP)
    USBPD_CC_STATE_ATTACHED_DFP = 0b010,  // 已附着为 Source (DFP)
    USBPD_CC_STATE_AUDIO_ACC = 0b100,     // 已连接音频附件
    USBPD_CC_STATE_DEBUG_ACC = 0b101,     // 已连接调试附件
    USBPD_CC_STATE_ERROR = 0b111,         // 错误或非法连接
};

/**
 * @brief USB PD 报文 SOP 类型枚举
 */
enum usbpd_sop_type_e {
    USBPD_SOP_TYPE_SOP = 0b000,               // SOP：对端端口
    USBPD_SOP_TYPE_SOP_PRIME = 0b001,         // SOP'：近端线缆插头 (E-Marker)
    USBPD_SOP_TYPE_SOP_DOUBLE_PRIME = 0b010,  // SOP''：远端线缆插头
    USBPD_SOP_TYPE_HARD_RESET = 0b100,        // 硬复位信号
    USBPD_SOP_TYPE_CABLE_RESET = 0b101,       // 线缆复位信号
    USBPD_SOP_TYPE_INVALID = 0b111,           // 无效消息类型
};

/**
 * @brief USB PD PHY 消息缓冲区结构
 */
struct usbpd_phy_buffer_t {
    enum usbpd_sop_type_e sop;            // 传输令牌
    union usbpd_message_header_u header;  // 消息头
    uint8_t payload[28];                     // 载荷
    uint16_t payload_len;                 // 载荷长度

    struct
    {
        uint8_t is_ready : 1;  // 1: 数据可读
        uint8_t reserved : 7;
    } flag;
};

/**
 * @brief USB PD 策略类型枚举
 *
 */
enum usbpd_pdo_strategy_t {
    UBSPD_PDO_STRATEGY_MAX_POWER,      // 纯粹功率优先：不管电压，选功率最大的
    UBSPD_PDO_STRATEGY_MAX_VOLTAGE,    // 纯粹电压优先：选电压最高的
    UBSPD_PDO_STRATEGY_MIN_VOLTAGE,    // 纯粹低压优先：通常用于测试或进入低功耗模式
    UBSPD_PDO_STRATEGY_PREFER_PPS,     // 优先选择 PPS
    UBSPD_PDO_STRATEGY_MATCH_VOLTAGE,  // 精确匹配：必须满足指定的电压值
    UBSPD_PDO_STRATEGY_VOLTAGE_WINDOW  // 窗口匹配：在 min_mv 到 max_mv 之间找功率最大的
};

/**
 * @brief USB PD 策略管理器配置结构体
 */
struct usbpd_pdo_manager_config_t {
    enum usbpd_pdo_strategy_t strategy; /* 搜索策略：最大功率/最高电压/窗口匹配等 */
    uint32_t target_mv;                 /* 目标电压 (用于 MATCH_VOLTAGE) */
    uint32_t min_mv;                    /* 窗口最低电压 */
    uint32_t max_mv;                    /* 窗口最高电压 */
    uint32_t min_ma;                    /* 最小电流需求 */
};

#define USBPD_OK 0     // 成功
#define USBPD_ERR -1   // 错误
#define USBPD_BUSY -2  // 忙

#endif                 // USB_PD_DEF_H */
