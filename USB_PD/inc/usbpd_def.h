/**
* @file usbpd_def.h
* @brief USB Power Delivery（USB PD）协议栈通用定义头文件
*
* 所属模块：USB PD 协议栈（驱动层，与具体控制器/平台无关）。
* 内容：USB PD Rev 3.2 V1.1 协议的核心类型定义，包括：
*   - 返回码与协议参数宏；
*   - 协议时序常量（单位：毫秒）；
*   - 报文类型枚举（控制/数据/扩展/扩展控制消息）；
*   - 报文头、PDO/RDO、各类数据对象（DO）的位域共用体；
*   - 各扩展消息数据块（Data Block）的结构体；
*   - 编译期大小静态断言（typedef char 数组技巧）。
*
* 设计约定：
*   - 位域定义要求小端序目标（非小端编译直接 #error 拒绝）；
*   - 位域字段注释统一标注位偏移（Bit N..M，小端序从 Bit0 起）；
*   - 字段单位后缀约定：10ma=10mA 步进、50mv=50mV 步进、100mwh=100mWh 步进、
*     20mv=20mV 步进、250mw=250mW 步进、w=1W 步进；
*   - 注释中的 Table 6.x 对应 USB PD 规范正文表编号。
*/
#ifndef USBPD_DEF_H
#define USBPD_DEF_H

#include <stdint.h>

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "usbpd bit-field definitions require a little-endian target"
#endif

#if defined(__GNUC__)
#define USBPD_PACKED __attribute__((packed))
#else
#define USBPD_PACKED
#endif

/* USB Power Delivery Revision 3.2, Version 1.1 protocol definitions. */

/* ===== 返回码定义 ===== */
/* 返回码约定：0 表示成功，负值表示各类错误 */
#define USBPD_OK                 (0)     /* 成功 */
#define USBPD_ERR                (-1)    /* 通用错误 */
#define USBPD_BUSY               (-2)    /* 忙，操作暂不可执行 */
#define USBPD_ERR_PARAM          (-3)    /* 参数错误 */
#define USBPD_ERR_UNSUPPORTED    (-4)    /* 不支持的功能/特性 */
#define USBPD_ERR_STATE          (-5)    /* 状态机状态不合法 */
#define USBPD_ERR_TIMEOUT        (-6)    /* 协议超时 */
#define USBPD_ERR_PROTOCOL       (-7)    /* 协议错误（帧格式/字段非法） */
#define USBPD_ERR_OVERFLOW       (-8)    /* 缓冲区溢出/数据越界 */

/* ===== 报文长度与数据对象数量上限 ===== */
#define USBPD_MAX_DATA_OBJ          7U      /* 单帧最大数据对象（DO）数量：7 个 */
#define USBPD_MAX_EPR_DATA_OBJ      11U     /* EPR（扩展功率范围）模式下最大数据对象数量：11 个 */
#define USBPD_CRC_LEN               4U      /* 帧尾部 CRC 长度：4 字节 */
#define USBPD_MAX_FRAME_LEN         30U     /* 单帧最大长度：30 字节（不含 CRC） */
#define USBPD_TRACE_FRAME_LEN       34U     /* 跟踪/抓包缓冲帧长度：34 字节（含 CRC） */
#define USBPD_EXT_DATA_MAX          260U    /* 扩展消息最大数据长度：260 字节 */
#define USBPD_EXT_CHUNK_DATA_MAX    26U     /* 扩展消息分块传输时单块最大数据长度：26 字节 */

/* Protocol timing values in milliseconds (Table 6.68). */
/* ===== 协议时序参数（单位：毫秒，对应规范 Table 6.68） ===== */
#define USBPD_T_RECEIVE_MS                  1U      /* 接收端最小有效位时间间隔（tReceive） */
#define USBPD_T_SENDER_RESPONSE_MIN_MS      27U     /* 发送方响应最小等待时间（tSenderResponse 下限） */
#define USBPD_T_SENDER_RESPONSE_MAX_MS      33U     /* 发送方响应最大等待时间（tSenderResponse 上限） */
#define USBPD_T_RECEIVER_RESPONSE_MS        15U     /* 接收方响应时间（tReceiverResponse） */
#define USBPD_T_SINK_REQUEST_MS             100U    /* Sink 发起 Request 的最小间隔（tSinkRequest） */
#define USBPD_T_CHUNK_SENDER_REQUEST_MS     30U     /* 分块传输发送方请求时间（tChunkSenderRequest） */
#define USBPD_T_CHUNK_SENDER_RESPONSE_MS    30U     /* 分块传输发送方响应时间（tChunkSenderResponse） */
#define USBPD_T_SINK_WAIT_CAP_MIN_MS        310U    /* Sink 等待 Source Capabilities 的最小超时（tSinkWaitCap 下限） */
#define USBPD_T_SINK_WAIT_CAP_MAX_MS        620U    /* Sink 等待 Source Capabilities 的最大超时（tSinkWaitCap 上限） */
#define USBPD_T_PS_TRANSITION_SPR_MS        500U    /* SPR 模式电源转换时间（tPSTransition） */
#define USBPD_T_PS_TRANSITION_SPR_MAX_MS    550U    /* SPR 模式电源转换最大时间 */
#define USBPD_T_PS_TRANSITION_EPR_MS        1020U   /* EPR 模式电源转换时间 */
#define USBPD_T_ENTER_EPR_MS                500U    /* 进入 EPR 模式的时间 */
#define USBPD_T_SINK_EPR_KEEPALIVE_MS       375U    /* Sink 侧 EPR KeepAlive 发送间隔 */
#define USBPD_T_SOURCE_EPR_KEEPALIVE_MS     875U    /* Source 侧 EPR KeepAlive 发送间隔 */
#define USBPD_T_PPS_REQUEST_MS              10000U  /* PPS（可编程电源）Request 更新最大间隔 */
#define USBPD_N_RETRY_COUNT                 2U      /* 消息重试次数上限 */
#define USBPD_N_HARD_RESET_COUNT            2U      /* Hard Reset 重试次数上限 */

/** @brief USB PD 协议版本（对应报文头 revision 字段） */
enum usbpd_revision_e
{
    USBPD_REV10 =         0U, /* 版本 1.0 */
    USBPD_REV20 =         1U, /* 版本 2.0 */
    USBPD_REV30 =         2U, /* 版本 3.0 */
    USBPD_REV_RESERVED =  3U, /* 保留值 */
};

/** @brief 电源角色（对应报文头 power_role 字段） */
enum usbpd_power_role_e
{
    USBPD_POWER_ROLE_SINK =    0U, /* Sink：受电方 */
    USBPD_POWER_ROLE_SOURCE =  1U, /* Source：供电方 */
};

/** @brief 数据角色（对应报文头 data_role 字段） */
enum usbpd_data_role_e
{
    USBPD_DATA_ROLE_UFP =  0U, /* UFP：上行端口（设备） */
    USBPD_DATA_ROLE_DFP =  1U, /* DFP：下行端口（主机） */
};

/** @brief VCONN 供电角色 */
enum usbpd_vconn_role_e
{
    USBPD_VCONN_SINK =    0U, /* VCONN Sink：消耗 VCONN 电源 */
    USBPD_VCONN_SOURCE =  1U, /* VCONN Source：为线缆/有源器件供电 */
};

/** @brief 端口类型 */
enum usbpd_port_type_e
{
    USBPD_PORT_SINK =    0U, /* 仅 Sink 端口 */
    USBPD_PORT_SOURCE,       /* 仅 Source 端口 */
    USBPD_PORT_DRP,          /* DRP：双角色端口 */
};

/** @brief SOP 类型（帧起始对象，用于区分协议层） */
enum usbpd_sop_e
{
    USBPD_SOP =              0U, /* 端口到端口（Port-to-Port） */
    USBPD_SOP_PRIME,             /* SOP'：端口到线缆（Cable） */
    USBPD_SOP_DPRIME,            /* SOP''：端口到线缆插头（Plug） */
    USBPD_SOP_HARD_RESET,        /* Hard Reset 信号 */
    USBPD_SOP_CABLE_RESET,       /* Cable Reset 信号 */
    USBPD_SOP_INVALID =      7U, /* 非法 SOP 值 */
};

/** @brief CC 引脚状态（Rp/Rd 检测） */
enum usbpd_cc_e
{
    USBPD_CC_OPEN =    0U, /* CC 开路（未连接） */
    USBPD_CC_RA,           /* Ra 下拉（线缆有源器件） */
    USBPD_CC_RD,           /* Rd 下拉（Sink） */
    USBPD_CC_RP_DEF =  5U, /* Rp 默认电流档 */
    USBPD_CC_RP_1A5,       /* Rp 1.5A 档 */
    USBPD_CC_RP_3A,        /* Rp 3A 档 */
};

/** @brief CC 引脚主动上下拉控制 */
enum usbpd_cc_pull_e
{
    USBPD_CC_PULL_OPEN =  0U, /* 不接上下拉 */
    USBPD_CC_PULL_RD,         /* 接 Rd 下拉（模拟 Sink） */
    USBPD_CC_PULL_RP,         /* 接 Rp 上拉（模拟 Source） */
};

/* Table 6.5. GotoMin and Ping remain encoded for protocol decoding only. */
/** @brief 控制消息类型（报文头 type=0，规范 Table 6.5）
*  GotoMin 与 Ping 仅保留用于协议解码（规范保留定义）。 */
enum usbpd_ctrl_e
{
    USBPD_CTRL_RESERVED =             0U,  /* 保留 */
    USBPD_CTRL_GOODCRC =              1U,  /* GoodCRC：链路层确认 */
    USBPD_CTRL_GOTO_MIN =             2U,  /* GotoMin：请求最小功率档 */
    USBPD_CTRL_ACCEPT =               3U,  /* Accept：接受消息 */
    USBPD_CTRL_REJECT =               4U,  /* Reject：拒绝消息 */
    USBPD_CTRL_PING =                 5U,  /* Ping：保活消息 */
    USBPD_CTRL_PS_RDY =               6U,  /* PS_RDY：电源就绪 */
    USBPD_CTRL_GET_SOURCE_CAP =       7U,  /* Get_Source_Cap：获取源能力 */
    USBPD_CTRL_GET_SINK_CAP =         8U,  /* Get_Sink_Cap：获取汇能力 */
    USBPD_CTRL_DR_SWAP =              9U,  /* DR_Swap：数据角色交换 */
    USBPD_CTRL_PR_SWAP =              10U, /* PR_Swap：电源角色交换 */
    USBPD_CTRL_VCONN_SWAP =           11U, /* VCONN_Swap：VCONN 角色交换 */
    USBPD_CTRL_WAIT =                 12U, /* Wait：请求方稍候 */
    USBPD_CTRL_SOFT_RESET =           13U, /* Soft_Reset：软复位 */
    USBPD_CTRL_DATA_RESET =           14U, /* Data_Reset：数据复位 */
    USBPD_CTRL_DATA_RESET_COMPLETE =  15U, /* Data_Reset_Complete：数据复位完成 */
    USBPD_CTRL_NOT_SUPPORTED =        16U, /* Not_Supported：不支持 */
    USBPD_CTRL_GET_SOURCE_CAP_EXT =   17U, /* Get_Source_Cap_Ext：获取扩展源能力 */
    USBPD_CTRL_GET_STATUS =           18U, /* Get_Status：获取状态 */
    USBPD_CTRL_FR_SWAP =              19U, /* FR_Swap：快速角色交换 */
    USBPD_CTRL_GET_PPS_STATUS =       20U, /* Get_PPS_Status：获取 PPS 状态 */
    USBPD_CTRL_GET_COUNTRY_CODES =    21U, /* Get_Country_Codes：获取国家代码列表 */
    USBPD_CTRL_GET_SINK_CAP_EXT =     22U, /* Get_Sink_Cap_Ext：获取扩展汇能力 */
    USBPD_CTRL_GET_SOURCE_INFO =      23U, /* Get_Source_Info：获取源信息 */
    USBPD_CTRL_GET_REVISION =         24U, /* Get_Revision：获取版本信息 */
    USBPD_CTRL_RESERVED_25 =          25U, /* 保留 */
    USBPD_CTRL_RESERVED_26 =          26U, /* 保留 */
    USBPD_CTRL_RESERVED_27 =          27U, /* 保留 */
    USBPD_CTRL_RESERVED_28 =          28U, /* 保留 */
    USBPD_CTRL_RESERVED_29 =          29U, /* 保留 */
    USBPD_CTRL_RESERVED_30 =          30U, /* 保留 */
    USBPD_CTRL_RESERVED_31 =          31U, /* 保留 */
};

/* Table 6.6. */
/** @brief 数据消息类型（报文头 type=1，规范 Table 6.6） */
enum usbpd_data_e
{
    USBPD_DATA_RESERVED =          0U,  /* 保留 */
    USBPD_DATA_SOURCE_CAP =        1U,  /* Source_Capabilities：源能力列表 */
    USBPD_DATA_REQUEST =           2U,  /* Request：请求功率 */
    USBPD_DATA_BIST =              3U,  /* BIST：内建自测 */
    USBPD_DATA_SINK_CAP =          4U,  /* Sink_Capabilities：汇能力列表 */
    USBPD_DATA_BATTERY_STATUS =    5U,  /* Battery_Status：电池状态 */
    USBPD_DATA_ALERT =             6U,  /* Alert：告警 */
    USBPD_DATA_GET_COUNTRY_INFO =  7U,  /* Get_Country_Info：获取国家信息 */
    USBPD_DATA_ENTER_USB =         8U,  /* Enter_USB：进入 USB 模式 */
    USBPD_DATA_EPR_REQUEST =       9U,  /* EPR_Request：EPR 请求 */
    USBPD_DATA_EPR_MODE =          10U, /* EPR_Mode：EPR 模式控制 */
    USBPD_DATA_SOURCE_INFO =       11U, /* Source_Info：源信息 */
    USBPD_DATA_REVISION =          12U, /* Revision：版本信息 */
    USBPD_DATA_RESERVED_13 =       13U, /* 保留 */
    USBPD_DATA_RESERVED_14 =       14U, /* 保留 */
    USBPD_DATA_VENDOR_DEFINED =    15U, /* Vendor_Defined：厂商自定义消息 */
};



/* Table 6.53. */
/** @brief 扩展消息类型（报文头 extended=1，规范 Table 6.53） */
enum usbpd_extended_e
{
    USBPD_EXT_RESERVED =                  0U,  /* 保留 */
    USBPD_EXT_SOURCE_CAP =                1U,  /* Source_Capabilities_Extended：扩展源能力 */
    USBPD_EXT_STATUS =                    2U,  /* Status：状态 */
    USBPD_EXT_GET_BATTERY_CAP =           3U,  /* Get_Battery_Cap：获取电池能力 */
    USBPD_EXT_GET_BATTERY_STATUS =        4U,  /* Get_Battery_Status：获取电池状态 */
    USBPD_EXT_BATTERY_CAP =               5U,  /* Battery_Cap：电池能力 */
    USBPD_EXT_GET_MANUFACTURER_INFO =     6U,  /* Get_Manufacturer_Info：获取厂商信息 */
    USBPD_EXT_MANUFACTURER_INFO =         7U,  /* Manufacturer_Info：厂商信息 */
    USBPD_EXT_SECURITY_REQUEST =          8U,  /* Security_Request：安全请求 */
    USBPD_EXT_SECURITY_RESPONSE =         9U,  /* Security_Response：安全响应 */
    USBPD_EXT_FIRMWARE_UPDATE_REQUEST =   10U, /* Firmware_Update_Request：固件更新请求 */
    USBPD_EXT_FIRMWARE_UPDATE_RESPONSE =  11U, /* Firmware_Update_Response：固件更新响应 */
    USBPD_EXT_PPS_STATUS =                12U, /* PPS_Status：PPS 状态 */
    USBPD_EXT_COUNTRY_INFO =              13U, /* Country_Info：国家信息 */
    USBPD_EXT_COUNTRY_CODES =             14U, /* Country_Codes：国家代码列表 */
    USBPD_EXT_SINK_CAP =                  15U, /* Sink_Capabilities_Extended：扩展汇能力 */
    USBPD_EXT_CONTROL =                   16U, /* Extended_Control：扩展控制 */
    USBPD_EXT_EPR_SOURCE_CAP =            17U, /* EPR_Source_Capabilities：EPR 源能力 */
    USBPD_EXT_EPR_SINK_CAP =              18U, /* EPR_Sink_Capabilities：EPR 汇能力 */
    USBPD_EXT_RESERVED_19 =               19U, /* 保留 */
    USBPD_EXT_RESERVED_20 =               20U, /* 保留 */
    USBPD_EXT_RESERVED_21 =               21U, /* 保留 */
    USBPD_EXT_RESERVED_22 =               22U, /* 保留 */
    USBPD_EXT_RESERVED_23 =               23U, /* 保留 */
    USBPD_EXT_RESERVED_24 =               24U, /* 保留 */
    USBPD_EXT_RESERVED_25 =               25U, /* 保留 */
    USBPD_EXT_RESERVED_26 =               26U, /* 保留 */
    USBPD_EXT_RESERVED_27 =               27U, /* 保留 */
    USBPD_EXT_RESERVED_28 =               28U, /* 保留 */
    USBPD_EXT_RESERVED_29 =               29U, /* 保留 */
    USBPD_EXT_VENDOR_DEFINED =            30U, /* Vendor_Defined：扩展厂商自定义 */
    USBPD_EXT_RESERVED_31 =               31U, /* 保留 */
};


/* Table 6.67, encoded in the two-byte Extended Control Data Block. */
/** @brief 扩展控制消息类型（编码在两字节 Extended Control Data Block 中，规范 Table 6.67） */
enum usbpd_extended_control_e
{
    USBPD_EXT_CTRL_RESERVED =            0U, /* 保留 */
    USBPD_EXT_CTRL_EPR_GET_SOURCE_CAP =  1U, /* EPR_Get_Source_Cap：EPR 获取源能力 */
    USBPD_EXT_CTRL_EPR_GET_SINK_CAP =    2U, /* EPR_Get_Sink_Cap：EPR 获取汇能力 */
    USBPD_EXT_CTRL_EPR_KEEPALIVE =       3U, /* EPR_KeepAlive：EPR 保活 */
    USBPD_EXT_CTRL_EPR_KEEPALIVE_ACK =   4U, /* EPR_KeepAlive_Ack：EPR 保活确认 */
};

/** @brief PDO 类型（编码在 PDO 高 2 位，规范 Table 6.9） */
enum usbpd_pdo_type_e
{
    USBPD_PDO_FIXED =     0U, /* 固定电压 PDO */
    USBPD_PDO_BATTERY =   1U, /* 电池 PDO */
    USBPD_PDO_VARIABLE =  2U, /* 可变电压（非电池）PDO */
    USBPD_PDO_APDO =      3U, /* APDO：增强型可编程 PDO（PPS/AVS） */
};


/** @brief APDO 子类型（编码在 APDO 的 subtype 字段） */
enum usbpd_apdo_type_e
{
    USBPD_APDO_PPS =       0U, /* PPS：可编程电源 */
    USBPD_APDO_EPR_AVS =   1U, /* EPR AVS：EPR 可调电压源 */
    USBPD_APDO_SPR_AVS =   2U, /* SPR AVS：SPR 可调电压源 */
    USBPD_APDO_RESERVED =  3U, /* 保留 */
};

#define USBPD_APDO_SPR_PPS    USBPD_APDO_PPS /* 兼容别名：SPR PPS 即标准 PPS */

/** @brief 峰值电流档位（PDO peak_current 字段） */
enum usbpd_peak_current_e
{
    USBPD_PEAK_CURRENT_NONE =  0U, /* 无峰值电流能力 */
    USBPD_PEAK_CURRENT_1,          /* 档位 1：110% 过载 */
    USBPD_PEAK_CURRENT_2,          /* 档位 2：125% 过载 */
    USBPD_PEAK_CURRENT_3,          /* 档位 3：150% 过载 */
};

/** @brief FR Swap（快速角色交换）电流能力 */
enum usbpd_frs_current_e
{
    USBPD_FRS_NOT_SUPPORTED =      0U, /* 不支持 FR Swap */
    USBPD_FRS_DEFAULT_USB_POWER,       /* 默认 USB 功率（500mA@5V） */
    /* 1.5A @ 5V */
    USBPD_FRS_1A5_AT_5V,
    /* 3A @ 5V */
    USBPD_FRS_3A_AT_5V,
};

/** @brief BIST（内建自测）模式（BIST DO 的 mode 字段） */
enum usbpd_bist_mode_e
{
    USBPD_BIST_CARRIER_MODE =            5U,  /* Carrier Mode 2：载波测试 */
    USBPD_BIST_TEST_DATA =               8U,  /* Test Data：测试数据模式 */
    USBPD_BIST_SHARED_TEST_MODE_ENTRY =  9U,  /* Shared Test Mode Entry：进入共享测试模式 */
    USBPD_BIST_SHARED_TEST_MODE_EXIT =   10U, /* Shared Test Mode Exit：退出共享测试模式 */
};

/** @brief VDM 命令类型（VDM 头 command_type 字段） */
enum usbpd_vdm_command_type_e
{
    USBPD_VDM_CMD_REQUEST =  0U, /* REQ：请求 */
    USBPD_VDM_CMD_ACK =      1U, /* ACK：确认 */
    USBPD_VDM_CMD_NAK =      2U, /* NAK：否定 */
    USBPD_VDM_CMD_BUSY =     3U, /* BUSY：忙 */
};

/** @brief 结构化 VDM 命令（VDM 头 command 字段） */
enum usbpd_vdm_command_e
{
    USBPD_VDM_DISCOVER_IDENTITY =  1U, /* Discover Identity：发现身份 */
    USBPD_VDM_DISCOVER_SVIDS =     2U, /* Discover SVIDs：发现 SVID 列表 */
    USBPD_VDM_DISCOVER_MODES =     3U, /* Discover Modes：发现模式 */
    USBPD_VDM_ENTER_MODE =         4U, /* Enter Mode：进入模式 */
    USBPD_VDM_EXIT_MODE =          5U, /* Exit Mode：退出模式 */
    USBPD_VDM_ATTENTION =          6U, /* Attention：注意/通知 */
};

/** @brief EPR 模式操作（EPR Mode DO 的 action 字段） */
enum usbpd_epr_mode_action_e
{
    USBPD_EPR_MODE_ENTER =            1U, /* Enter：请求进入 EPR */
    USBPD_EPR_MODE_ENTER_ACK =        2U, /* Enter_Ack：确认进入 */
    USBPD_EPR_MODE_ENTER_SUCCEEDED =  3U, /* Enter_Succeeded：进入成功 */
    USBPD_EPR_MODE_ENTER_FAILED =     4U, /* Enter_Failed：进入失败 */
    USBPD_EPR_MODE_EXIT =             5U, /* Exit：退出 EPR */
};

/** @brief EPR 模式进入失败原因 */
enum usbpd_epr_mode_failure_e
{
    USBPD_EPR_FAIL_UNKNOWN =            0U, /* 未知原因 */
    USBPD_EPR_FAIL_CABLE_NOT_CAPABLE =  1U, /* 线缆不支持 EPR */
    USBPD_EPR_FAIL_VCONN_SOURCE =       2U, /* VCONN Source 能力不足 */
    USBPD_EPR_FAIL_RDO_NOT_CAPABLE =    3U, /* RDO 不支持 EPR */
    USBPD_EPR_FAIL_SOURCE_ENTRY =       4U, /* Source 进入 EPR 失败 */
    USBPD_EPR_FAIL_PDO_NOT_CAPABLE =    5U, /* PDO 不支持 EPR */
};

/** @brief 告警类型位（Alert DO 的 type 字段，按位 OR） */
enum usbpd_alert_type_e
{
    USBPD_ALERT_BATTERY =              (1U << 1), /* Bit1：电池告警 */
    USBPD_ALERT_OCP =                  (1U << 2), /* Bit2：过流保护告警 */
    USBPD_ALERT_OTP =                  (1U << 3), /* Bit3：过温保护告警 */
    USBPD_ALERT_OPERATING_CONDITION =  (1U << 4), /* Bit4：工作条件告警 */
    USBPD_ALERT_SOURCE_INPUT =         (1U << 5), /* Bit5：源输入告警 */
    USBPD_ALERT_OVP =                  (1U << 6), /* Bit6：过压保护告警 */
    USBPD_ALERT_EXTENDED =             (1U << 7), /* Bit7：扩展告警 */
};

/** @brief USB PD 报文头（2 字节）
*  位域布局（小端序）：type:5 | data_role:1 | revision:2 | power_role:1 |
*  message_id:3 | data_objects:3 | extended:1。 */
union usbpd_header_u {
    
    uint16_t raw;      /* 原始值视图 */
    uint8_t  bytes[2]; /* 字节视图（小端序） */
    struct
    {
        
        uint16_t type           : 5; /* Bit0..4 : 消息类型（0=控制 1=数据；若 extended=1 则为扩展消息类型） */
        uint16_t data_role      : 1; /* Bit5 : 数据角色（0=UFP 1=DFP） */
        uint16_t revision       : 2; /* Bit6..7 : 协议版本（见 usbpd_revision_e） */
        uint16_t power_role     : 1; /* Bit8 : 电源角色（0=Sink 1=Source） */
        uint16_t message_id     : 3; /* Bit9..11 : 消息序号（0..7 循环使用） */
        uint16_t data_objects   : 3; /* Bit12..14 : 数据对象（DO）数量 */
        uint16_t extended       : 1; /* Bit15 : 扩展消息标志（1=扩展消息） */
    } bits;
};

/** @brief 扩展消息头（2 字节）
*  位域布局（小端序）：data_size:9 | reserved:1 | request_chunk:1 |
*  chunk_number:4 | chunked:1。 */
union usbpd_ext_header_u {
    uint16_t raw;       /* 原始值视图 */
    uint8_t  bytes[2];  /* 字节视图（小端序） */
    struct
    {
        uint16_t data_size          : 9; /* Bit0..8 : 扩展消息数据长度（字节） */
        uint16_t reserved           : 1; /* Bit9 : 保留 */
        uint16_t request_chunk      : 1; /* Bit10 : Request Chunk 请求标志 */
        uint16_t chunk_number       : 4; /* Bit11..14 : 分块序号 */
        uint16_t chunked            : 1; /* Bit15 : 分块传输标志（1=数据被分块） */
    } bits;
};

/** @brief 电源数据对象 PDO（4 字节）
*  通过不同子结构表达固定/电池/可变/APDO(PPS/AVS) 等类型，type 字段位于 Bit30..31。
*  位偏移自 Bit0 起（小端序）。 */
union usbpd_pdo_u {
    uint32_t raw;       /* 原始值视图 */
    uint8_t  bytes[4];  /* 字节视图（小端序） */
    struct
    {
        uint32_t current_10ma           : 10; /* Bit0..9 : 最大电流（10mA 步进） */
        uint32_t voltage_50mv           : 10; /* Bit10..19 : 电压（50mV 步进） */
        uint32_t peak_current           : 2;  /* Bit20..21 : 峰值电流档位（见 usbpd_peak_current_e） */
        uint32_t reserved0              : 1;  /* Bit22 : 保留 */
        uint32_t epr_capable            : 1;  /* Bit23 : EPR 模式能力 */
        uint32_t unchunked              : 1;  /* Bit24 : 支持非分块扩展消息 */
        uint32_t drd                    : 1;  /* Bit25 : DRD 双数据角色能力 */
        uint32_t usb_comm               : 1;  /* Bit26 : USB 通信能力 */
        uint32_t unconstrained          : 1;  /* Bit27 : 不受约束电源 */
        uint32_t suspend                : 1;  /* Bit28 : 支持 Suspend */
        uint32_t drp                    : 1;  /* Bit29 : DRP 双角色电源能力 */
        uint32_t type                   : 2;  /* Bit30..31 : PDO 类型（见 usbpd_pdo_type_e） */
    } fixed;
    struct
    {
        uint32_t current_10ma           : 10; /* Bit0..9 : 最大电流（10mA 步进） */
        uint32_t voltage_50mv           : 10; /* Bit10..19 : 电压（50mV 步进） */
        uint32_t reserved0              : 3;  /* Bit20..22 : 保留 */
        uint32_t frs_current            : 2;  /* Bit23..24 : FR Swap 电流能力（见 usbpd_frs_current_e） */
        uint32_t drd                    : 1;  /* Bit25 : DRD 双数据角色能力 */
        uint32_t usb_comm               : 1;  /* Bit26 : USB 通信能力 */
        uint32_t unconstrained          : 1;  /* Bit27 : 不受约束电源 */
        uint32_t higher_capability      : 1;  /* Bit28 : 更高能力标志 */
        uint32_t drp                    : 1;  /* Bit29 : DRP 双角色电源能力 */
        uint32_t type                   : 2;  /* Bit30..31 : PDO 类型 */
    } sink_fixed;
    struct
    {
        uint32_t current_10ma           : 10; /* Bit0..9 : 最大电流（10mA 步进） */
        uint32_t min_voltage_50mv       : 10; /* Bit10..19 : 最小电压（50mV 步进） */
        uint32_t max_voltage_50mv       : 10; /* Bit20..29 : 最大电压（50mV 步进） */
        uint32_t type                   : 2;  /* Bit30..31 : PDO 类型 */
    } variable;
    struct
    {
        uint32_t power_250mw            : 10; /* Bit0..9 : 最大功率（250mW 步进） */
        uint32_t min_voltage_50mv       : 10; /* Bit10..19 : 最小电压（50mV 步进） */
        uint32_t max_voltage_50mv       : 10; /* Bit20..29 : 最大电压（50mV 步进） */
        uint32_t type                   : 2;  /* Bit30..31 : PDO 类型 */
    } battery;
    struct
    {
        uint32_t current_50ma           : 7;  /* Bit0..6 : 最大电流（50mA 步进） */
        uint32_t reserved0              : 1;  /* Bit7 : 保留 */
        uint32_t min_voltage_100mv      : 8;  /* Bit8..15 : 最小电压（100mV 步进） */
        uint32_t reserved1              : 1;  /* Bit16 : 保留 */
        uint32_t max_voltage_100mv      : 8;  /* Bit17..24 : 最大电压（100mV 步进） */
        uint32_t reserved2              : 2;  /* Bit25..26 : 保留 */
        uint32_t power_limited          : 1;  /* Bit27 : 功率受限标志 */
        uint32_t subtype                : 2;  /* Bit28..29 : APDO 子类型（见 usbpd_apdo_type_e） */
        uint32_t type                   : 2;  /* Bit30..31 : PDO 类型 */
    } pps;
    struct
    {
        uint32_t current_50ma           : 7;  /* Bit0..6 : 最大电流（50mA 步进） */
        uint32_t reserved0              : 1;  /* Bit7 : 保留 */
        uint32_t min_voltage_100mv      : 8;  /* Bit8..15 : 最小电压（100mV 步进） */
        uint32_t reserved1              : 1;  /* Bit16 : 保留 */
        uint32_t max_voltage_100mv      : 8;  /* Bit17..24 : 最大电压（100mV 步进） */
        uint32_t reserved2              : 3;  /* Bit25..27 : 保留 */
        uint32_t subtype                : 2;  /* Bit28..29 : APDO 子类型 */
        uint32_t type                   : 2;  /* Bit30..31 : PDO 类型 */
    } sink_pps;
    struct
    {
        uint32_t current_20v_10ma       : 10; /* Bit0..9 : 20V 档最大电流（10mA 步进） */
        uint32_t current_15v_10ma       : 10; /* Bit10..19 : 15V 档最大电流（10mA 步进） */
        uint32_t reserved0              : 6;  /* Bit20..25 : 保留 */
        uint32_t peak_current           : 2;  /* Bit26..27 : 峰值电流档位 */
        uint32_t subtype                : 2;  /* Bit28..29 : APDO 子类型 */
        uint32_t type                   : 2;  /* Bit30..31 : PDO 类型 */
    } spr_avs;
    struct
    {
        uint32_t current_20v_10ma       : 10; /* Bit0..9 : 20V 档最大电流（10mA 步进） */
        uint32_t current_15v_10ma       : 10; /* Bit10..19 : 15V 档最大电流（10mA 步进） */
        uint32_t reserved0              : 8;  /* Bit20..27 : 保留 */
        uint32_t subtype                : 2;  /* Bit28..29 : APDO 子类型 */
        uint32_t type                   : 2;  /* Bit30..31 : PDO 类型 */
    } sink_spr_avs;
    struct
    {
        uint32_t pdp_w                  : 8;  /* Bit0..7 : PDP（总功率，1W 步进） */
        uint32_t min_voltage_100mv      : 8;  /* Bit8..15 : 最小电压（100mV 步进） */
        uint32_t reserved0              : 1;  /* Bit16 : 保留 */
        uint32_t max_voltage_100mv      : 9;  /* Bit17..25 : 最大电压（100mV 步进） */
        uint32_t peak_current           : 2;  /* Bit26..27 : 峰值电流档位 */
        uint32_t subtype                : 2;  /* Bit28..29 : APDO 子类型 */
        uint32_t type                   : 2;  /* Bit30..31 : PDO 类型 */
    } avs;
    struct
    {
        uint32_t pdp_w                  : 8;  /* Bit0..7 : PDP（总功率，1W 步进） */
        uint32_t min_voltage_100mv      : 8;  /* Bit8..15 : 最小电压（100mV 步进） */
        uint32_t reserved0              : 1;  /* Bit16 : 保留 */
        uint32_t max_voltage_100mv      : 9;  /* Bit17..25 : 最大电压（100mV 步进） */
        uint32_t reserved1              : 2;  /* Bit26..27 : 保留 */
        uint32_t subtype                : 2;  /* Bit28..29 : APDO 子类型 */
        uint32_t type                   : 2;  /* Bit30..31 : PDO 类型 */
    } sink_avs;
    struct
    {
        uint32_t fields                 : 28; /* Bit0..27 : 原始负载字段（按实际类型解析） */
        uint32_t subtype                : 2;  /* Bit28..29 : APDO 子类型 */
        uint32_t type                   : 2;  /* Bit30..31 : PDO 类型 */
    } common;
};

/** @brief 请求数据对象 RDO（4 字节）
*  object_position 指定所请求的 PDO 序号（1..7）。 */
union usbpd_rdo_u {
    uint32_t raw;       /* 原始值视图 */
    uint8_t  bytes[4];  /* 字节视图（小端序） */
    struct
    {
        uint32_t max_current_10ma            : 10; /* Bit0..9 : 最大电流（10mA 步进） */
        uint32_t operating_current_10ma      : 10; /* Bit10..19 : 运行电流（10mA 步进） */
        uint32_t reserved                    : 2;  /* Bit20..21 : 保留 */
        uint32_t epr_capable                 : 1;  /* Bit22 : EPR 模式能力 */
        uint32_t unchunked                   : 1;  /* Bit23 : 请求非分块扩展消息 */
        uint32_t no_suspend                  : 1;  /* Bit24 : 不允许 Suspend */
        uint32_t usb_comm                    : 1;  /* Bit25 : USB 通信请求 */
        uint32_t mismatch                    : 1;  /* Bit26 : 能力不匹配标志 */
        uint32_t give_back                   : 1;  /* Bit27 : GiveBack 能力 */
        uint32_t object_position             : 4;  /* Bit28..31 : 目标 PDO 序号（1..7） */
    } fixed;
    struct
    {
        uint32_t max_power_250mw             : 10; /* Bit0..9 : 最大功率（250mW 步进） */
        uint32_t operating_power_250mw       : 10; /* Bit10..19 : 运行功率（250mW 步进） */
        uint32_t reserved                    : 2;  /* Bit20..21 : 保留 */
        uint32_t epr_capable                 : 1;  /* Bit22 : EPR 模式能力 */
        uint32_t unchunked                   : 1;  /* Bit23 : 请求非分块扩展消息 */
        uint32_t no_suspend                  : 1;  /* Bit24 : 不允许 Suspend */
        uint32_t usb_comm                    : 1;  /* Bit25 : USB 通信请求 */
        uint32_t mismatch                    : 1;  /* Bit26 : 能力不匹配标志 */
        uint32_t give_back                   : 1;  /* Bit27 : GiveBack 能力 */
        uint32_t object_position             : 4;  /* Bit28..31 : 目标 PDO 序号 */
    } battery;
    struct
    {
        uint32_t operating_current_50ma      : 7;  /* Bit0..6 : 运行电流（50mA 步进） */
        uint32_t reserved0                   : 2;  /* Bit7..8 : 保留 */
        uint32_t output_voltage_20mv         : 12; /* Bit9..20 : 输出电压（20mV 步进） */
        uint32_t reserved1                   : 1;  /* Bit21 : 保留 */
        uint32_t epr_capable                 : 1;  /* Bit22 : EPR 模式能力 */
        uint32_t unchunked                   : 1;  /* Bit23 : 请求非分块扩展消息 */
        uint32_t no_suspend                  : 1;  /* Bit24 : 不允许 Suspend */
        uint32_t usb_comm                    : 1;  /* Bit25 : USB 通信请求 */
        uint32_t mismatch                    : 1;  /* Bit26 : 能力不匹配标志 */
        uint32_t reserved2                   : 1;  /* Bit27 : 保留 */
        uint32_t object_position             : 4;  /* Bit28..31 : 目标 PDO 序号 */
    } pps;
    struct
    {
        uint32_t operating_current_50ma      : 7;  /* Bit0..6 : 运行电流（50mA 步进） */
        uint32_t reserved0                   : 2;  /* Bit7..8 : 保留 */
        uint32_t output_voltage_25mv         : 12; /* Bit9..20 : 输出电压（25mV 步进） */
        uint32_t reserved1                   : 1;  /* Bit21 : 保留 */
        uint32_t epr_capable                 : 1;  /* Bit22 : EPR 模式能力 */
        uint32_t unchunked                   : 1;  /* Bit23 : 请求非分块扩展消息 */
        uint32_t no_suspend                  : 1;  /* Bit24 : 不允许 Suspend */
        uint32_t usb_comm                    : 1;  /* Bit25 : USB 通信请求 */
        uint32_t mismatch                    : 1;  /* Bit26 : 能力不匹配标志 */
        uint32_t reserved2                   : 1;  /* Bit27 : 保留 */
        uint32_t object_position             : 4;  /* Bit28..31 : 目标 PDO 序号 */
    } avs;
};

/** @brief BIST 数据对象（4 字节） */
union usbpd_bist_do_u {
    uint32_t raw;  /* 原始值视图 */
    struct
    {
        uint32_t reserved      : 28; /* Bit0..27 : 保留 */
        uint32_t mode          : 4;  /* Bit28..31 : BIST 模式（见 usbpd_bist_mode_e） */
    } bits;
};

/** @brief Battery Status 数据对象（4 字节） */
union usbpd_battery_status_do_u {
    uint32_t raw;  /* 原始值视图 */
    struct
    {
        uint32_t reserved                     : 8;  /* Bit0..7 : 保留 */
        uint32_t battery_info                 : 8;  /* Bit8..15 : 电池信息（编码见 usbpd_battery_info_u） */
        uint32_t present_capacity_100mwh      : 16; /* Bit16..31 : 当前容量（100mWh 步进） */
    } bits;
};

/** @brief Alert 数据对象（4 字节） */
union usbpd_alert_do_u {
    uint32_t raw;  /* 原始值视图 */
    struct
    {
        uint32_t extended_event_type          : 4;  /* Bit0..3 : 扩展事件类型 */
        uint32_t reserved0                    : 12; /* Bit4..15 : 保留 */
        uint32_t hot_swappable_batteries      : 4;  /* Bit16..19 : 可热插拔电池位图 */
        uint32_t fixed_batteries              : 4;  /* Bit20..23 : 固定电池位图 */
        uint32_t type                         : 8;  /* Bit24..31 : 告警类型位图（见 usbpd_alert_type_e） */
    } bits;
};

/** @brief Get Country Info 数据对象（4 字节） */
union usbpd_get_country_info_do_u {
    uint32_t raw;  /* 原始值视图 */
    struct
    {
        uint32_t country_code      : 16; /* Bit0..15 : 国家代码（ISO 3166-1） */
        uint32_t reserved          : 16; /* Bit16..31 : 保留 */
    } bits;
};

/** @brief Enter USB 数据对象（4 字节） */
union usbpd_enter_usb_do_u {
    uint32_t raw;  /* 原始值视图 */
    struct
    {
        uint32_t reserved0          : 13; /* Bit0..12 : 保留 */
        uint32_t host_present       : 1;  /* Bit13 : 主机存在标志 */
        uint32_t tbt                : 1;  /* Bit14 : Thunderbolt 支持 */
        uint32_t dp                 : 1;  /* Bit15 : DisplayPort 支持 */
        uint32_t pcie               : 1;  /* Bit16 : PCIe 支持 */
        uint32_t cable_current      : 2;  /* Bit17..18 : 线缆电流能力 */
        uint32_t cable_type         : 2;  /* Bit19..20 : 线缆类型 */
        uint32_t cable_speed        : 3;  /* Bit21..23 : 线缆速度 */
        uint32_t reserved1          : 1;  /* Bit24 : 保留 */
        uint32_t usb3_drd           : 1;  /* Bit25 : USB3 DRD 支持 */
        uint32_t usb4_drd           : 1;  /* Bit26 : USB4 DRD 支持 */
        uint32_t reserved2          : 1;  /* Bit27 : 保留 */
        uint32_t usb_mode           : 3;  /* Bit28..30 : USB 模式 */
        uint32_t reserved3          : 1;  /* Bit31 : 保留 */
    } bits;
};

/** @brief EPR Mode 数据对象（4 字节） */
union usbpd_epr_mode_do_u {
    uint32_t raw;  /* 原始值视图 */
    struct
    {
        uint32_t reserved      : 16; /* Bit0..15 : 保留 */
        uint32_t data          : 8;  /* Bit16..23 : 数据（与 action 配合，含失败原因） */
        uint32_t action        : 8;  /* Bit24..31 : 操作（见 usbpd_epr_mode_action_e） */
    } bits;
};

/** @brief Source Info 数据对象（4 字节） */
union usbpd_source_info_do_u {
    uint32_t raw;  /* 原始值视图 */
    struct
    {
        uint32_t reported_pdp_w      : 8; /* Bit0..7 : 上报 PDP（1W 步进） */
        uint32_t present_pdp_w       : 8; /* Bit8..15 : 当前 PDP（1W 步进） */
        uint32_t maximum_pdp_w       : 8; /* Bit16..23 : 最大 PDP（1W 步进） */
        uint32_t reserved            : 7; /* Bit24..30 : 保留 */
        uint32_t port_type           : 1; /* Bit31 : 端口类型 */
    } bits;
};

/** @brief Revision 数据对象（4 字节） */
union usbpd_revision_do_u {
    uint32_t raw;  /* 原始值视图 */
    struct
    {
        uint32_t reserved            : 16; /* Bit0..15 : 保留 */
        uint32_t version_minor       : 4;  /* Bit16..19 : 版本次号 */
        uint32_t version_major       : 4;  /* Bit20..23 : 版本主号 */
        uint32_t revision_minor      : 4;  /* Bit24..27 : 修订次号 */
        uint32_t revision_major      : 4;  /* Bit28..31 : 修订主号 */
    } bits;
};

/** @brief VDM 头（4 字节）：结构化 / 非结构化两种视图 */
union usbpd_vdm_header_u {
    uint32_t raw;  /* 原始值视图 */
    struct
    {
        uint32_t command              : 5;  /* Bit0..4 : VDM 命令（见 usbpd_vdm_command_e） */
        uint32_t reserved0            : 1;  /* Bit5 : 保留 */
        uint32_t command_type         : 2;  /* Bit6..7 : 命令类型（见 usbpd_vdm_command_type_e） */
        uint32_t object_position      : 3;  /* Bit8..10 : 对象序号 */
        uint32_t reserved1            : 2;  /* Bit11..12 : 保留 */
        uint32_t version              : 2;  /* Bit13..14 : VDM 版本 */
        uint32_t structured           : 1;  /* Bit15 : 结构化标志（1=结构化 VDM） */
        uint32_t svid                 : 16; /* Bit16..31 : SVID（标准厂商 ID） */
    } structured;
    struct
    {
        uint32_t vendor_defined       : 15; /* Bit0..14 : 厂商自定义数据 */
        uint32_t structured           : 1;  /* Bit15 : 结构化标志（0=非结构化 VDM） */
        uint32_t vid                  : 16; /* Bit16..31 : VID（厂商 ID） */
    } unstructured;
};

/** @brief 电池充电状态（Battery Info 的 charging_status 字段） */
enum usbpd_battery_charging_status_e
{
    USBPD_BATTERY_CHARGING =         0U, /* 充电中 */
    USBPD_BATTERY_DISCHARGING =      1U, /* 放电中 */
    USBPD_BATTERY_IDLE =             2U, /* 空闲 */
    USBPD_BATTERY_CHARGE_RESERVED =  3U, /* 保留 */
};

/** @brief 温度状态 */
enum usbpd_temperature_status_e
{
    USBPD_TEMP_NOT_SUPPORTED =     0U, /* 不支持温度报告 */
    USBPD_TEMP_NORMAL =            1U, /* 正常 */
    USBPD_TEMP_WARNING =           2U, /* 警告 */
    USBPD_TEMP_OVER_TEMPERATURE =  3U, /* 过温 */
};

/** @brief 电源状态（Power State Change 的 power_state 字段） */
enum usbpd_power_state_e
{
    USBPD_POWER_STATE_NOT_SUPPORTED =   0U, /* 不支持电源状态报告 */
    USBPD_POWER_STATE_S0 =              1U, /* S0：运行态 */
    USBPD_POWER_STATE_MODERN_STANDBY =  2U, /* 现代待机（Modern Standby） */
    USBPD_POWER_STATE_S3 =              3U, /* S3：睡眠 */
    USBPD_POWER_STATE_S4 =              4U, /* S4：休眠 */
    USBPD_POWER_STATE_S5 =              5U, /* S5：关机 */
    USBPD_POWER_STATE_G3 =              6U, /* G3：机械断电 */
};

/** @brief 厂商信息请求目标 */
enum usbpd_manufacturer_target_e
{
    USBPD_MANUFACTURER_PORT_OR_CABLE =  0U, /* 目标为端口或线缆 */
    USBPD_MANUFACTURER_BATTERY =        1U, /* 目标为电池 */
};

/** @brief 电池信息字节（1 字节，Battery Status DO 的 battery_info 字段） */
union usbpd_battery_info_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t invalid_reference      : 1; /* Bit0 : 无效引用标志 */
        uint8_t battery_present        : 1; /* Bit1 : 电池在位标志 */
        uint8_t charging_status        : 2; /* Bit2..3 : 充电状态（见 usbpd_battery_charging_status_e） */
        uint8_t reserved               : 4; /* Bit4..7 : 保留 */
    } bits;
};

/** @brief Status 消息：当前输入状态字节 */
union usbpd_status_present_input_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t reserved0                       : 1; /* Bit0 : 保留 */
        uint8_t external_power                  : 1; /* Bit1 : 外部电源在位 */
        uint8_t external_power_ac               : 1; /* Bit2 : 外部交流电源在位 */
        uint8_t battery_power                   : 1; /* Bit3 : 电池供电中 */
        uint8_t internal_non_battery_power      : 1; /* Bit4 : 内部非电池电源 */
        uint8_t reserved1                       : 3; /* Bit5..7 : 保留 */
    } bits;
};

/** @brief Status 消息：事件标志字节 */
union usbpd_status_event_flags_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t reserved0               : 1; /* Bit0 : 保留 */
        uint8_t ocp                     : 1; /* Bit1 : 过流事件 */
        uint8_t otp                     : 1; /* Bit2 : 过温事件 */
        uint8_t ovp                     : 1; /* Bit3 : 过压事件 */
        uint8_t current_limit_mode      : 1; /* Bit4 : 限流模式 */
        uint8_t reserved1               : 3; /* Bit5..7 : 保留 */
    } bits;
};

/** @brief Status 消息：温度状态字节 */
union usbpd_status_temperature_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t reserved0      : 1; /* Bit0 : 保留 */
        uint8_t status         : 2; /* Bit1..2 : 温度状态（见 usbpd_temperature_status_e） */
        uint8_t reserved1      : 5; /* Bit3..7 : 保留 */
    } bits;
};

/** @brief Status 消息：电源限制字节 */
union usbpd_status_power_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t reserved0                      : 1; /* Bit0 : 保留 */
        uint8_t limited_by_cable               : 1; /* Bit1 : 受线缆限制 */
        uint8_t limited_by_other_ports         : 1; /* Bit2 : 受其他端口限制 */
        uint8_t limited_by_external_power      : 1; /* Bit3 : 受外部电源限制 */
        uint8_t limited_by_event               : 1; /* Bit4 : 受事件限制 */
        uint8_t limited_by_temperature         : 1; /* Bit5 : 受温度限制 */
        uint8_t reserved1                      : 2; /* Bit6..7 : 保留 */
    } bits;
};

/** @brief Status 消息：电源状态变化字节 */
union usbpd_status_power_state_change_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t power_state      : 3; /* Bit0..2 : 电源状态（见 usbpd_power_state_e） */
        uint8_t indicator        : 3; /* Bit3..5 : 状态指示器 */
        uint8_t reserved         : 2; /* Bit6..7 : 保留 */
    } bits;
};

/** @brief PPS Status 消息：状态标志字节 */
union usbpd_pps_status_flags_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t reserved0               : 1; /* Bit0 : 保留 */
        uint8_t temperature_status      : 2; /* Bit1..2 : 温度状态（见 usbpd_temperature_status_e） */
        uint8_t current_limit_mode      : 1; /* Bit3 : 限流模式 */
        uint8_t reserved1               : 4; /* Bit4..7 : 保留 */
    } bits;
};

/** @brief 源电压调节特性字节（Source Capabilities Extended） */
union usbpd_source_voltage_regulation_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t load_step_slew_rate       : 2; /* Bit0..1 : 负载阶跃压摆率 */
        uint8_t load_step_90_percent      : 1; /* Bit2 : 90% 负载阶跃能力 */
        uint8_t reserved                  : 5; /* Bit3..7 : 保留 */
    } bits;
};

/** @brief 负载阶跃特性字节（Sink Capabilities Extended） */
union usbpd_load_step_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t slew_rate      : 2; /* Bit0..1 : 压摆率 */
        uint8_t reserved       : 6; /* Bit2..7 : 保留 */
    } bits;
};

/** @brief 合规性字节（LPS/PS1/PS2） */
union usbpd_compliance_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t lps           : 1; /* Bit0 : LPS（有限功率源）合规 */
        uint8_t ps1           : 1; /* Bit1 : PS1 合规 */
        uint8_t ps2           : 1; /* Bit2 : PS2 合规 */
        uint8_t reserved      : 5; /* Bit3..7 : 保留 */
    } bits;
};

/** @brief 接触电流字节 */
union usbpd_touch_current_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t low_touch_current      : 1; /* Bit0 : 低接触电流 */
        uint8_t ground_pin             : 1; /* Bit1 : 接地引脚 */
        uint8_t protective_earth       : 1; /* Bit2 : 保护接地 */
        uint8_t reserved               : 5; /* Bit3..7 : 保留 */
    } bits;
};

/** @brief 峰值电流特性（2 字节） */
union usbpd_peak_current_u {
    uint16_t raw;  /* 原始值视图 */
    struct
    {
        uint16_t overload_percent_10pct      : 5; /* Bit0..4 : 过载百分比（10% 步进） */
        uint16_t overload_period_20ms        : 6; /* Bit5..10 : 过载持续时间（20ms 步进） */
        uint16_t duty_cycle_5pct             : 4; /* Bit11..14 : 占空比（5% 步进） */
        uint16_t vbus_droop                  : 1; /* Bit15 : VBUS 跌落标志 */
    } bits;
};

/** @brief 源输入字节 */
union usbpd_source_inputs_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t external_supply             : 1; /* Bit0 : 外部电源输入 */
        uint8_t external_unconstrained      : 1; /* Bit1 : 外部不受限电源 */
        uint8_t internal_battery            : 1; /* Bit2 : 内部电池输入 */
        uint8_t reserved                    : 5; /* Bit3..7 : 保留 */
    } bits;
};

/** @brief 电池槽位字节 */
union usbpd_battery_slots_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t fixed_batteries          : 4; /* Bit0..3 : 固定电池数量 */
        uint8_t hot_swappable_slots      : 4; /* Bit4..7 : 可热插拔槽位数量 */
    } bits;
};

/** @brief Sink 模式字节 */
union usbpd_sink_modes_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t pps_charging           : 1; /* Bit0 : PPS 充电支持 */
        uint8_t vbus_powered           : 1; /* Bit1 : VBUS 供电 */
        uint8_t ac_powered             : 1; /* Bit2 : 交流供电 */
        uint8_t battery_powered        : 1; /* Bit3 : 电池供电 */
        uint8_t battery_unlimited      : 1; /* Bit4 : 电池容量不受限 */
        uint8_t avs                    : 1; /* Bit5 : AVS 支持 */
        uint8_t reserved               : 2; /* Bit6..7 : 保留 */
    } bits;
};

/** @brief SPR PDP 字节（1W 步进） */
union usbpd_spr_pdp_u {
    uint8_t raw;  /* 原始值视图 */
    struct
    {
        uint8_t watts         : 7; /* Bit0..6 : 功率（1W 步进） */
        uint8_t reserved      : 1; /* Bit7 : 保留 */
    } bits;
};

/** @brief 扩展控制数据块（2 字节，Extended Control 消息负载，规范 Table 6.67） */
struct usbpd_extended_control_db_t
{
    uint8_t type;  /* 扩展控制类型（见 usbpd_extended_control_e） */
    uint8_t data;  /* 与类型相关的数据 */
};

/* Table 6.55, seven-byte SOP Status Data Block. */
/** @brief Status 消息的 7 字节 SOP Status 数据块（规范 Table 6.55） */
struct USBPD_PACKED usbpd_status_db_t
{
    uint8_t                                 internal_temperature_c;  /* 内部温度（摄氏度） */
    union usbpd_status_present_input_u      present_input;           /* 当前输入状态 */
    uint8_t                                 present_battery_input;   /* 当前电池输入（百分比 0..100） */
    union usbpd_status_event_flags_u        event_flags;             /* 事件标志（OCP/OTP/OVP 等） */
    union usbpd_status_temperature_u        temperature_status;      /* 温度状态 */
    union usbpd_status_power_u              power_status;            /* 电源限制状态 */
    union usbpd_status_power_state_change_u power_state_change;      /* 电源状态变化 */
};

/* Tables 6.57 and 6.58. Values 0..3 are fixed and 4..7 are hot-swap. */
/** @brief Get Battery Cap/Status 请求数据块（规范 Table 6.57/6.58，0..3 为固定电池、4..7 为热插拔槽位） */
struct usbpd_get_battery_db_t
{
    uint8_t battery_reference;  /* 电池引用序号（0..7） */
};

/* Table 6.59, nine-byte Battery Capability Data Block. */
/** @brief 电池能力数据块（9 字节，Battery Cap 消息负载，规范 Table 6.59） */
struct USBPD_PACKED usbpd_battery_cap_db_t
{
    uint16_t vid;                               /* 电池厂商 ID */
    uint16_t pid;                               /* 电池产品 ID */
    uint16_t design_capacity_100mwh;            /* 设计容量（100mWh 步进） */
    uint16_t last_full_charge_capacity_100mwh;  /* 上次满充容量（100mWh 步进） */
    uint8_t  invalid_reference;                 /* 无效引用标志（1=引用无效） */
};

/* Table 6.60, two-byte Get Manufacturer Info Data Block. */
/** @brief Get Manufacturer Info 请求数据块（2 字节，规范 Table 6.60） */
struct usbpd_get_manufacturer_info_db_t
{
    uint8_t target;     /* 目标对象（见 usbpd_manufacturer_target_e） */
    uint8_t reference;  /* 引用序号 */
};

/* Table 6.61. Data Size is 5..26 bytes including the terminating NUL. */
/** @brief 厂商信息数据块（规范 Table 6.61，数据长度 5..26 字节含末尾 NUL） */
struct USBPD_PACKED usbpd_manufacturer_info_db_t
{
    uint16_t vid;                      /* 厂商 ID */
    uint16_t pid;                      /* 产品 ID */
    char     manufacturer_string[22];  /* 厂商信息字符串（最长 22 字节，含终止 NUL） */
};

/* Table 6.63. Data Size is 4..26 bytes and length is the code count. */
/** @brief 国家代码列表数据块（规范 Table 6.63，数据长度 4..26 字节，length 为国家代码个数） */
struct usbpd_country_codes_db_t
{
    uint8_t length;             /* 国家代码个数 */
    uint8_t reserved;           /* 保留 */
    uint8_t country_codes[24];  /* 国家代码数组（每项 2 字节 ISO 3166-1） */
};

/* Table 6.64. Data Size is 4..26 bytes. */
/** @brief 国家信息数据块（规范 Table 6.64，数据长度 4..26 字节） */
struct usbpd_country_info_db_t
{
    uint8_t country_code[2];            /* 国家代码（ISO 3166-1 字母） */
    uint8_t reserved[2];                /* 保留 */
    uint8_t country_specific_data[22];  /* 国家特定数据 */
};

/* Table 6.54, 25-byte Source Capabilities Extended Data Block. */
/** @brief 源能力扩展数据块（25 字节，Source_Capabilities_Extended 消息负载，规范 Table 6.54） */
struct USBPD_PACKED usbpd_source_cap_ext_db_t
{
    uint16_t                                vid;                 /* 厂商 ID */
    uint16_t                                pid;                 /* 产品 ID */
    uint32_t                                xid;                 /* 扩展 ID */
    uint8_t                                 fw_version;          /* 固件版本 */
    uint8_t                                 hw_version;          /* 硬件版本 */
    union usbpd_source_voltage_regulation_u voltage_regulation;  /* 电压调节特性 */
    uint8_t                                 holdup_time;         /* 保持时间（毫秒） */
    union usbpd_compliance_u                compliance;          /* 合规性（LPS/PS1/PS2） */
    union usbpd_touch_current_u             touch_current;       /* 接触电流 */
    union usbpd_peak_current_u              peak_current1;       /* 峰值电流档 1 */
    union usbpd_peak_current_u              peak_current2;       /* 峰值电流档 2 */
    union usbpd_peak_current_u              peak_current3;       /* 峰值电流档 3 */
    uint8_t                                 touch_temp;          /* 接触温度（摄氏度） */
    union usbpd_source_inputs_u             source_inputs;       /* 源输入 */
    union usbpd_battery_slots_u             batteries;           /* 电池槽位 */
    union usbpd_spr_pdp_u                   spr_source_pdp;      /* SPR 源 PDP（1W 步进） */
    uint8_t                                 epr_source_pdp_w;    /* EPR 源 PDP（1W 步进） */
};

/* Table 6.65, 24-byte Sink Capabilities Extended Data Block. */
/** @brief 汇能力扩展数据块（24 字节，Sink_Capabilities_Extended 消息负载，规范 Table 6.65） */
struct USBPD_PACKED usbpd_sink_cap_ext_db_t
{
    uint16_t                    vid;                         /* 厂商 ID */
    uint16_t                    pid;                         /* 产品 ID */
    uint32_t                    xid;                         /* 扩展 ID */
    uint8_t                     fw_version;                  /* 固件版本 */
    uint8_t                     hw_version;                  /* 硬件版本 */
    uint8_t                     skedb_version;               /* SKEDB 版本 */
    union usbpd_load_step_u     load_step;                   /* 负载阶跃特性 */
    union usbpd_peak_current_u  sink_load_characteristics;   /* Sink 负载特性（峰值电流） */
    union usbpd_compliance_u    compliance;                  /* 合规性 */
    uint8_t                     touch_temp;                  /* 接触温度（摄氏度） */
    union usbpd_battery_slots_u battery_info;                /* 电池槽位信息 */
    union usbpd_sink_modes_u    sink_modes;                  /* Sink 模式 */
    union usbpd_spr_pdp_u       spr_sink_minimum_pdp;        /* SPR Sink 最小 PDP（1W 步进） */
    union usbpd_spr_pdp_u       spr_sink_operational_pdp;    /* SPR Sink 运行 PDP（1W 步进） */
    union usbpd_spr_pdp_u       spr_sink_maximum_pdp;        /* SPR Sink 最大 PDP（1W 步进） */
    uint8_t                     epr_sink_minimum_pdp_w;      /* EPR Sink 最小 PDP（1W 步进） */
    uint8_t                     epr_sink_operational_pdp_w;  /* EPR Sink 运行 PDP（1W 步进） */
    uint8_t                     epr_sink_maximum_pdp_w;      /* EPR Sink 最大 PDP（1W 步进） */
};

/* Table 6.62, four-byte PPS Status Data Block. */
/** @brief PPS 状态数据块（4 字节，PPS_Status 消息负载，规范 Table 6.62） */
struct usbpd_pps_status_db_t
{
    uint16_t                       output_voltage_20mv;  /* 输出电压（20mV 步进） */
    uint8_t                        output_current_50ma;  /* 输出电流（50mA 步进） */
    union usbpd_pps_status_flags_u flags;                /* 状态标志 */
};

/** @brief USB PD 单帧缓冲结构（协议栈内部收发缓冲） */
struct usbpd_frame_t
{
    union usbpd_header_u header;       /* 报文头 */
    /* 负载：数据对象数组（最多 7 个 × 4 字节） */
    uint8_t payload[USBPD_MAX_DATA_OBJ * 4U];
    uint8_t              payload_len;  /* 负载有效长度（字节） */
    uint8_t              sop;          /* SOP 类型（见 usbpd_sop_e） */
    uint8_t              raw_len;      /* 原始帧长度（字节） */
};

/* ===== 编译期大小断言（大小不符时数组长度为负，编译报错） ===== */
/* 以下 typedef char 数组技巧在编译期校验各类型/结构体大小 */
typedef char usbpd_header_size_must_be_2[(sizeof(union usbpd_header_u) == 2U) ? 1 : -1];
typedef char usbpd_ext_header_size_must_be_2[(sizeof(union usbpd_ext_header_u) == 2U) ? 1 : -1];
typedef char usbpd_pdo_size_must_be_4[(sizeof(union usbpd_pdo_u) == 4U) ? 1 : -1];
typedef char usbpd_rdo_size_must_be_4[(sizeof(union usbpd_rdo_u) == 4U) ? 1 : -1];
typedef char usbpd_vdm_header_size_must_be_4[(sizeof(union usbpd_vdm_header_u) == 4U) ? 1 : -1];
typedef char usbpd_epr_mode_size_must_be_4[(sizeof(union usbpd_epr_mode_do_u) == 4U) ? 1 : -1];
typedef char usbpd_source_cap_ext_size_must_be_25[(sizeof(struct usbpd_source_cap_ext_db_t) == 25U) ? 1 : -1];
typedef char usbpd_sink_cap_ext_size_must_be_24[(sizeof(struct usbpd_sink_cap_ext_db_t) == 24U) ? 1 : -1];
typedef char usbpd_status_db_size_must_be_7[(sizeof(struct usbpd_status_db_t) == 7U) ? 1 : -1];
typedef char usbpd_battery_cap_size_must_be_9[(sizeof(struct usbpd_battery_cap_db_t) == 9U) ? 1 : -1];
typedef char usbpd_pps_status_size_must_be_4[(sizeof(struct usbpd_pps_status_db_t) == 4U) ? 1 : -1];
typedef char usbpd_manufacturer_info_size_must_be_26[(sizeof(struct usbpd_manufacturer_info_db_t) == 26U) ? 1 : -1];
typedef char usbpd_country_codes_size_must_be_26[(sizeof(struct usbpd_country_codes_db_t) == 26U) ? 1 : -1];
typedef char usbpd_country_info_size_must_be_26[(sizeof(struct usbpd_country_info_db_t) == 26U) ? 1 : -1];

#endif
