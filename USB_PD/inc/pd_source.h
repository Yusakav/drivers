/**
* @file pd_source.h
* @brief USB PD Source（供电方）设备策略层对外接口声明
*
* 所属模块：USB PD 协议栈（策略层，基于协议层实现 Source 状态机）。
* 对应实现：pd_source.c；依赖回调接口：usbpd_dpm.h。
*
* Source 策略层职责：
*   - 实现 USB PD 3.2 规范 Source 状态机（Unattached → Attach_Wait → Startup →
*     Send_Capabilities → Wait_Request → Transition → Ready / Soft Reset / Hard Reset / Error Recovery）；
*   - 管理 Source PDO 宣告、Request/RDO 校验与 Accept/Reject 回复；
*   - 通过 DPM 回调控制板级电源输出（set_source / source_ready）；
*   - 处理 PPS 动态请求超时与 Soft Reset / Hard Reset 流程。
*
* 使用约定：
*   1. 构造 pd_source_config_t（PDO 列表、能力标志、DPM 回调，set_source 必须非 NULL）；
*   2. 调用 pd_source_init() 初始化（内部会初始化 PHY 与协议层，并设置 Rp）；
*   3. 主循环以毫秒节拍调用 pd_source_task()；
*   4. 事件经 DPM 回调通知应用层。
*/
#ifndef PD_SOURCE_H
#define PD_SOURCE_H

#include "usbpd_def.h"
#include "usbpd_dpm.h"

/** @brief Source 状态机状态（对应 pd_source_task() switch 分支） */
enum pd_source_state_e
{
    PD_SOURCE_UNATTACHED = 0U,      /* 未连接（无 Rd） */
    PD_SOURCE_ATTACH_WAIT,          /* CC 去抖动等待（检测到 Rd 后延时确认） */
    PD_SOURCE_STARTUP,              /* Startup：5V VBUS 稳定后进入 Send_Capabilities */
    PD_SOURCE_SEND_CAPABILITIES,    /* 发送 Source_Capabilities 宣告 */
    PD_SOURCE_WAIT_REQUEST,         /* 等待 Sink 发送 Request */
    PD_SOURCE_TRANSITION,           /* 已回复 Accept，等待 Sink Accept + PS_RDY 并切换电压 */
    PD_SOURCE_READY,                /* 合约就绪，PPS 超时监控 */
    PD_SOURCE_SOFT_RESET,           /* Soft Reset 处理（发起或响应） */
    PD_SOURCE_HARD_RESET,           /* Hard Reset 处理（计数重试） */
    PD_SOURCE_ERROR_RECOVERY,       /* 错误恢复等待后回到 Unattached */
};

/** @brief Source 能力标志位（配置时填充） */
struct pd_source_features_t
{
    uint8_t pps : 1;              /* 支持 PPS（可编程电源）APDO */
    uint8_t avs : 1;              /* 支持 SPR AVS（可调电压源）APDO */
    uint8_t usb_communications : 1; /* 支持 USB 通信 */
    uint8_t drp : 1;              /* 支持 DRP（双角色端口） */
    uint8_t reserved : 4;         /* 保留位 */
};

/** @brief Source 策略层初始化配置 */
struct pd_source_config_t
{
    union usbpd_pdo_u source_pdo[USBPD_MAX_DATA_OBJ];   /* Source 宣告的 PDO 列表（SPR 模式，首项必须 5V Fixed） */
    union usbpd_pdo_u sink_pdo[USBPD_MAX_DATA_OBJ];     /* Source 宣告的 Sink PDO 列表（VCONN 供电场景） */
    struct usbpd_source_cap_ext_db_t source_cap_ext;     /* Source_Capabilities_Extended 数据块 */
    struct usbpd_sink_cap_ext_db_t sink_cap_ext;         /* Sink_Capabilities_Extended 数据块（DRP 角色） */
    union usbpd_source_info_do_u source_info;            /* Source_Info 数据对象 */
    uint8_t source_pdo_count;      /* source_pdo 有效数量（1..USBPD_MAX_DATA_OBJ） */
    uint8_t sink_pdo_count;        /* sink_pdo 有效数量（0..USBPD_MAX_DATA_OBJ） */
    uint8_t source_cap_ext_valid;  /* source_cap_ext 是否有效（1=有效） */
    uint8_t sink_cap_ext_valid;    /* sink_cap_ext 是否有效（1=有效） */
    uint8_t source_info_valid;     /* source_info 是否有效（1=有效） */
    struct pd_source_features_t features; /* 能力标志 */
    struct usbpd_dpm_callbacks_t dpm;   /* DPM 回调集合（set_source 必须非 NULL） */
};

/** @brief Source 策略层运行时状态（可通过 pd_source_get_status 查询） */
struct pd_source_status_t
{
    uint32_t negotiated_voltage_mv;  /* 当前协商电压（mV，0=无合约） */
    uint32_t negotiated_current_ma;  /* 当前协商电流（mA，0=无合约） */
    uint32_t vbus_mv;                /* VBUS 实际电压（mV，由 PHY 层采集） */
    uint8_t selected_pdo;            /* 当前选中 PDO 序号（1..7，0=无） */
    uint8_t state : 4;               /* 当前状态机状态（见 pd_source_state_e） */
    uint8_t contract_valid : 1;      /* 合约有效标志 */
    uint8_t pps_active : 1;          /* 当前处于 PPS 动态调整模式 */
    uint8_t attached : 1;            /* 端口已连接（RP/RD 检测完成） */
    uint8_t reserved : 1;            /* 保留位 */
};

/**
 * @brief  查询端口是否已连接
 * @return 1=已连接（状态 != Unattached），0=未连接
 */
uint8_t pd_source_is_attached(void);

/**
 * @brief  查询 Source 运行时状态快照
 * @param status  输出状态结构（见 pd_source_status_t）
 * @return USBPD_OK 成功；USBPD_ERR_PARAM 参数为 NULL
 */
int pd_source_get_status(struct pd_source_status_t *status);

/**
 * @brief  手动发送控制消息（调试/测试用）
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param type  控制消息类型（见 usbpd_ctrl_e）
 * @return 协议层返回码
 */
int pd_source_send_control(uint8_t sop, uint8_t type);

/**
 * @brief  手动发送数据消息（调试/测试用）
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param type  数据消息类型（见 usbpd_data_e）
 * @param objects  数据对象数组
 * @param count  对象数量
 * @return 协议层返回码
 */
int pd_source_send_data_objects(uint8_t sop, uint8_t type, const uint32_t *objects, uint8_t count);

/**
 * @brief  手动发送扩展消息（调试/测试用）
 * @param sop  SOP 类型
 * @param type  扩展消息类型
 * @param data  扩展数据缓冲
 * @param length  数据长度
 * @return 协议层返回码
 */
int pd_source_send_extended(uint8_t sop, uint8_t type, const uint8_t *data, uint16_t length);

/**
 * @brief  初始化 Source 策略层（完整初始化：配置 + PHY + 协议层）
 *
 * @param config  初始化配置（set_source 回调必须非 NULL）
 * @return USBPD_OK 成功；USBPD_ERR_PARAM 配置非法；USBPD_ERR PHY 初始化失败
 */
int pd_source_init(const struct pd_source_config_t *config);

/**
 * @brief  初始化 Source 策略层（可控是否初始化底层硬件）
 *
 * @param config  初始化配置
 * @param initialize_hardware  1=同时初始化 PHY 与观察器，0=仅配置策略层
 * @return USBPD_OK 成功；USBPD_ERR_PARAM 配置非法；USBPD_ERR PHY 初始化失败
 */
int pd_source_policy_init(const struct pd_source_config_t *config, uint8_t initialize_hardware);

/**
 * @brief  Source 策略层周期任务（需在主循环中以毫秒节拍调用）
 *
 * 内部依次调用 PHY 层任务、协议层任务、待发送回复调度、CC/VBUS 检测、
 * 状态机迁移、PPS 超时监控、Soft-Hard Reset 处理。
 *
 * @param now_ms  当前系统时间（毫秒）
 */
void pd_source_task(uint32_t now_ms);

#endif
