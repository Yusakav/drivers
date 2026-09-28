/**
* @file pd_sink.h
* @brief USB PD Sink（受电方）设备策略层对外接口声明
*
* 所属模块：USB PD 协议栈（策略层，基于协议层实现 Sink 状态机）。
* 对应实现：pd_sink.c；依赖回调接口：usbpd_dpm.h。
*
* Sink 策略层职责：
*   - 实现 USB PD 3.2 规范 Sink 状态机（Unattached → Discovery → Requested →
*     Transition → Ready / Soft Reset / Hard Reset / Error Recovery 等）；
*   - 管理 Source PDO 收集、PDO 选择与 Request/RDO 构造；
*   - 支持 PPS / SPR AVS / EPR 模式的动态电压电流调整与 KeepAlive；
*   - 经 usbpd_dpm_callbacks_t 将事件与消息回调上报应用层。
*
* 使用约定：
*   1. 构造 pd_sink_config_t（PDO 列表、能力标志、DPM 回调）；
*   2. 调用 pd_sink_init() 初始化（内部会初始化 PHY 与协议层）；
*   3. 主循环以毫秒节拍调用 pd_sink_task()；
*   4. 通过 pd_sink_request() 发起功率请求，事件经 DPM 回调通知。
*/
#ifndef PD_SINK_H
#define PD_SINK_H

#include "usbpd_def.h"
#include "usbpd_dpm.h"

/** @brief Sink 状态机状态（对应 pd_sink_task() switch 分支） */
enum pd_sink_state_e
{
    PD_SINK_UNATTACHED = 0U,    /* 未连接（无 Rp） */
    PD_SINK_ATTACH_WAIT,        /* CC 去抖动等待（检测到 Rp 后延时确认） */
    PD_SINK_DISCOVERY,          /* Discovery：等待 Source_Capabilities 或主动请求 */
    PD_SINK_EPR_CAPS,           /* EPR：等待 EPR_Source_Capabilities */
    PD_SINK_EPR_ENTER,          /* EPR：等待 Source 对 EPR_Mode(Enter) 的响应 */
    PD_SINK_REQUESTED,          /* Request 已发出，等待 Accept/Reject/Wait */
    PD_SINK_TRANSITION,         /* 收到 Accept，等待 PS_RDY（电源转换） */
    PD_SINK_READY,              /* 合约就绪，PPS/A VS 动态调整与 EPR KeepAlive */
    PD_SINK_SOFT_RESET,         /* Soft Reset 处理（发起或响应） */
    PD_SINK_HARD_RESET,         /* Hard Reset 处理（计数重试） */
    PD_SINK_ERROR_RECOVERY,     /* 错误恢复等待后回到 Unattached */
};

/** @brief Sink 能力标志位（配置时填充） */
struct pd_sink_features_t
{
    uint8_t pps : 1;              /* 支持 PPS（可编程电源）APDO */
    uint8_t epr : 1;              /* 支持 EPR（扩展功率范围）模式 */
    uint8_t usb_communications : 1; /* 支持 USB 通信 */
    uint8_t trace_print : 1;      /* 启用调试跟踪输出 */
    uint8_t avs : 1;              /* 支持 SPR AVS（可调电压源）APDO */
    uint8_t reserved : 3;         /* 保留位 */
};

/** @brief Sink 策略层初始化配置 */
struct pd_sink_config_t
{
    union usbpd_pdo_u sink_pdo[USBPD_MAX_DATA_OBJ];   /* Sink 宣告的 PDO 列表（SPR 模式） */
    union usbpd_pdo_u epr_sink_pdo[USBPD_MAX_EPR_DATA_OBJ]; /* Sink 宣告的 EPR PDO 列表 */
    struct usbpd_sink_cap_ext_db_t sink_cap_ext;       /* Sink_Capabilities_Extended 数据块 */
    uint32_t max_voltage_mv;     /* 最大允许电压（mV） */
    uint32_t max_current_ma;     /* 最大允许电流（mA） */
    uint32_t max_power_mw;       /* 最大允许功率（mW） */
    uint8_t sink_pdo_count;      /* sink_pdo 有效数量（1..USBPD_MAX_DATA_OBJ） */
    uint8_t epr_sink_pdo_count;  /* epr_sink_pdo 有效数量（0=不支持 EPR，8..11 才合法） */
    uint8_t sink_cap_ext_valid;  /* sink_cap_ext 是否有效（1=有效） */
    struct pd_sink_features_t features; /* 能力标志 */
    struct usbpd_dpm_callbacks_t dpm;   /* DPM 回调集合 */
};

/** @brief Sink 策略层运行时状态（可通过 pd_sink_get_status 查询） */
struct pd_sink_status_t
{
    uint32_t requested_voltage_mv;   /* 应用层请求电压（mV） */
    uint32_t requested_current_ma;   /* 应用层请求电流（mA） */
    uint32_t negotiated_voltage_mv;  /* 当前协商电压（mV，0=无合约） */
    uint32_t negotiated_current_ma;  /* 当前协商电流（mA，0=无合约） */
    uint32_t vbus_mv;                /* VBUS 实际电压（mV，由 PHY 层采集） */
    uint8_t selected_pdo;            /* 当前选中 PDO 序号（1..7，0=无） */
    uint8_t source_pdo_count;        /* 对端 Source_Capabilities 中 PDO 数量 */
    uint8_t state : 4;               /* 当前状态机状态（见 pd_sink_state_e） */
    uint8_t contract_valid : 1;      /* 合约有效标志 */
    uint8_t fallback_active : 1;     /* 当前为 5V 兜底合约 */
    uint8_t epr_active : 1;          /* 当前处于 EPR 模式 */
    uint8_t reserved : 1;            /* 保留位 */
};

/**
 * @brief  查询端口是否已连接
 * @return 1=已连接（状态 != Unattached），0=未连接
 */
uint8_t pd_sink_is_attached(void);

/**
 * @brief  发起功率请求（仅登记目标，实际发送由状态机在下一机会触发）
 *
 * @param voltage_mv  目标电压（mV）
 * @param current_ma  目标电流（mA）
 * @return USBPD_OK 请求已登记；USBPD_ERR 参数越界（超出本 Sink 能力范围）
 */
int pd_sink_request(uint32_t voltage_mv, uint32_t current_ma);

/**
 * @brief  查询 Sink 运行时状态快照
 *
 * @param status  输出状态结构（见 pd_sink_status_t）
 * @return USBPD_OK 成功；USBPD_ERR 参数为 NULL
 */
int pd_sink_get_status(struct pd_sink_status_t *status);

/**
 * @brief  手动发送控制消息（调试/测试用）
 *
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param type  控制消息类型（见 usbpd_ctrl_e）
 * @return 协议层返回码（USBPD_OK / USBPD_ERR_PARAM / USBPD_BUSY）
 */
int pd_sink_send_control(uint8_t sop, uint8_t type);

/**
 * @brief  手动发送数据消息（调试/测试用）
 *
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param type  数据消息类型（见 usbpd_data_e）
 * @param objects  数据对象数组（小端序 32 位字）
 * @param count  数据对象数量
 * @return 协议层返回码
 */
int pd_sink_send_data_objects(uint8_t sop, uint8_t type, const uint32_t *objects, uint8_t count);

/**
 * @brief  手动发送扩展消息（调试/测试用）
 *
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param type  扩展消息类型（见 usbpd_extended_e）
 * @param data  扩展数据缓冲
 * @param length  数据长度（字节）
 * @return 协议层返回码
 */
int pd_sink_send_extended(uint8_t sop, uint8_t type, const uint8_t *data, uint16_t length);

/**
 * @brief  初始化 Sink 策略层（完整初始化：配置 + PHY + 协议层）
 *
 * @param config  初始化配置（可为 NULL，此时使用 5V/1A 默认值）
 * @return USBPD_OK 成功；USBPD_ERR_PARAM 配置非法；USBPD_ERR PHY 初始化失败
 */
int pd_sink_init(const struct pd_sink_config_t *config);

/**
 * @brief  初始化 Sink 策略层（可控是否初始化底层硬件）
 *
 * @param config  初始化配置（可为 NULL）
 * @param initialize_hardware  1=同时初始化 PHY 与观察器，0=仅配置策略层
 * @return USBPD_OK 成功；USBPD_ERR_PARAM 配置非法；USBPD_ERR PHY 初始化失败
 */
int pd_sink_policy_init(const struct pd_sink_config_t *config, uint8_t initialize_hardware);

/**
 * @brief  Sink 策略层周期任务（需在主循环中以毫秒节拍调用）
 *
 * 内部依次调用 PHY 层任务、协议层任务、待发送回复调度、CC/VBUS 检测、
 * 状态机迁移、EPR KeepAlive / PPS 动态请求 / Soft-Hard Reset 处理。
 *
 * @param now_ms  当前系统时间（毫秒）
 */
void pd_sink_task(uint32_t now_ms);

#endif
