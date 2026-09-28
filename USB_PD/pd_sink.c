/**
* @file pd_sink.c
* @brief USB PD Sink（受电方）设备策略层实现
*
* 所属模块：USB PD 协议栈（策略层，基于协议层实现 Sink 状态机）。
* 对应头文件：pd_sink.h；依赖：usbpd_protocol.h / usbpd_phy_ch32x035.h / usbpd_dpm.h。
*
* 本文件实现：
*   - Sink 状态机（11 状态）的完整迁移逻辑；
*   - Source PDO 收集、PDO 选择与 Request/RDO 构造（支持 Fixed/Battery/Variable/PPS/AVS/EPR AVS）；
*   - EPR 模式进入/退出流程、EPR KeepAlive；
*   - Soft Reset / Hard Reset / Error Recovery 处理；
*   - DPM 消息回调包装、事件回调包装、扩展消息响应填充；
*   - 应用层请求接口：pd_sink_is_attached / pd_sink_request / pd_sink_get_status / pd_sink_send_*。
*
* 注意：所有内部静态函数均以 sink_ 前缀命名，不对外暴露。
*/
#include "pd_sink.h"
#include "usbpd_phy_ch32x035.h"
#include "usbpd_protocol.h"
#include "usbpd_message.h"
#include <string.h>

/* ===== Sink 策略层私有时序参数 ===== */
#define PD_SINK_CC_DEBOUNCE_MS 100U                              /* CC 去抖动时间（毫秒） */
#define PD_SINK_WAIT_CAP_MS USBPD_T_SINK_WAIT_CAP_MIN_MS        /* 等待 Source_Capabilities 下限（毫秒） */
#define PD_SINK_SENDER_RESPONSE_MS USBPD_T_SENDER_RESPONSE_MAX_MS /* 对端响应最大等待（毫秒） */
#define PD_SINK_PS_TRANSITION_MS USBPD_T_PS_TRANSITION_SPR_MS   /* SPR 模式电源转换时间（毫秒） */
#define PD_SINK_ERROR_RECOVERY_MS 30U                            /* 错误恢复等待时间（毫秒） */
#define PD_SINK_EPR_KEEPALIVE_MS USBPD_T_SINK_EPR_KEEPALIVE_MS  /* EPR KeepAlive 发送间隔（毫秒） */
#define PD_SINK_MAX_WAIT_RETRIES 2U                              /* Wait 消息重试次数上限 */

/* ===== 内部枚举 ===== */
/** @brief PDO 选择类型（RDO 构造时决定使用 rdo.fixed / rdo.pps / rdo.avs 等 union 分支） */
enum pd_sink_select_e
{
    PD_SELECT_NONE = 0U,    /* 未选择 */
    PD_SELECT_FIXED,        /* Fixed PDO */
    PD_SELECT_VARIABLE,     /* Variable PDO */
    PD_SELECT_BATTERY,      /* Battery PDO */
    PD_SELECT_PPS,          /* PPS APDO */
    PD_SELECT_AVS           /* SPR AVS / EPR AVS */
};

/** @brief 待回复消息类型（sink_service_reply() switch 分支） */
enum pd_sink_reply_e
{
    PD_REPLY_NONE = 0U,         /* 无待回复 */
    PD_REPLY_SINK_CAP,          /* 回复 Sink_Capabilities */
    PD_REPLY_SINK_CAP_EXT,      /* 回复 Sink_Capabilities_Extended */
    PD_REPLY_EPR_SINK_CAP,      /* 回复 EPR_Sink_Capabilities */
    PD_REPLY_REVISION,          /* 回复 Revision */
    PD_REPLY_EXTENDED,          /* 回复 DPM 提供的扩展消息 */
    PD_REPLY_REJECT,            /* 回复 Reject */
    PD_REPLY_VDM_NAK,           /* 回复 VDM NAK（结构化 VDM 请求） */
    PD_REPLY_NOT_SUPPORTED      /* 回复 Not_Supported */
};

/* ===== 内部状态结构 ===== */
/** @brief Sink 策略层全局运行上下文（单例） */
struct pd_sink_t
{
    struct pd_sink_config_t config;               /* 初始化配置快照 */
    struct pd_sink_status_t status;               /* 对外可见运行时状态 */
    union usbpd_pdo_u source_pdo[USBPD_MAX_DATA_OBJ];        /* 对端 Source PDO 缓存（SPR 模式） */
    union usbpd_pdo_u epr_source_pdo[USBPD_MAX_EPR_DATA_OBJ]; /* 对端 EPR Source PDO 缓存 */
    uint8_t source_count;                         /* source_pdo 有效数量 */
    uint8_t epr_source_count;                     /* epr_source_pdo 有效数量 */
    uint8_t select_kind : 3;                      /* 当前 PDO 选择类型（见 pd_sink_select_e） */
    uint8_t source_valid : 1;                     /* source_pdo 已收集有效 */
    uint8_t epr_caps_valid : 1;                   /* epr_source_pdo 已收集有效 */
    uint8_t target_dirty : 1;                     /* 应用层请求目标已变更，需重新选择 */
    uint8_t get_source_cap_sent : 1;              /* 已发送 Get_Source_Cap */
    uint8_t get_epr_cap_sent : 1;                 /* 已发送 EPR_Get_Source_Cap */
    uint8_t epr_mode_sent : 1;                    /* 已发送 EPR_Mode */
    uint8_t epr_enter_acked : 1;                  /* 已收到 EPR_Mode(Enter_Ack) */
    uint8_t epr_keepalive_pending : 1;            /* 已发送 EPR KeepAlive，等待 ACK */
    uint8_t epr_target_pending : 1;               /* EPR 目标电压未达成，需先进 5V */
    uint8_t epr_exit_pending : 1;                 /* EPR 退出待处理（先协商 SPR 再 Exit） */
    uint8_t peer_soft_reset : 1;                  /* 对端发起 Soft Reset，待回复 Accept */
    uint8_t soft_reset_sent : 1;                  /* 本端已发送 Soft Reset，等待 Accept */
    uint8_t wait_pending : 1;                     /* 收到 Wait，等待 tSinkRequest 后重试 */
    uint8_t pending_reply;                        /* 待回复消息类型（见 pd_sink_reply_e） */
    uint8_t wait_retries : 2;                     /* Wait 已重试次数 */
    uint8_t hard_reset_count : 2;                 /* Hard Reset 已发送次数（上限 USBPD_N_HARD_RESET_COUNT） */
    uint8_t selected_index;                       /* 对端 PDO 列表中选中的序号（0 基） */
    uint8_t pending_vdm_sop;                      /* 待回复 VDM 的 SOP */
    uint32_t pending_vdm;                        /* 待回复 VDM 原始值（NAK 构造） */
    uint8_t pending_ext_type;                     /* 待回复扩展消息类型 */
    uint16_t pending_ext_length;                  /* 待回复扩展消息长度 */
    uint8_t pending_ext_data[USBPD_EXT_DATA_MAX]; /* 待回复扩展消息数据缓冲 */
    uint32_t selected_voltage_mv;                 /* 当前选中电压（mV，已对齐 APDO 步长） */
    uint32_t selected_current_ma;                 /* 当前选中电流（mA，已对齐步长） */
    uint32_t deadline_ms;                         /* 当前状态截止时间（毫秒） */
    uint32_t epr_keepalive_ms;                    /* 下次 EPR KeepAlive 发送时间（毫秒） */
    uint32_t pps_request_ms;                      /* 下次 PPS 动态请求时间（毫秒） */
    uint32_t now_ms;                              /* 最近一次 task 调度时的系统时间（毫秒） */
};

static struct pd_sink_t g_sink;  /* Sink 策略层全局上下文单例 */

/* ===== 内部辅助函数 ===== */
/**
 * @brief  判断当前状态 deadline 是否到期（毫秒回绕安全）
 * @param now  当前系统时间（毫秒）
 * @return 1=已到期，0=未到期
 */
static uint8_t sink_deadline(uint32_t now)
{
    return ((int32_t)(now - g_sink.deadline_ms) >= 0);
}

/**
 * @brief  两 32 位值取较小者
 */
static uint32_t sink_min(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

/**
 * @brief  判断 CC 引脚是否检测到 Rp（≥ USBPD_CC_RP_DEF）
 * @param cc  CC 引脚状态（见 usbpd_cc_e）
 * @return 1=检测到 Rp，0=未检测到
 */
static uint8_t sink_is_rp(enum usbpd_cc_e cc)
{
    return cc >= USBPD_CC_RP_DEF;
}

/**
 * @brief  DPM 事件回调包装（向应用层转发事件）
 *
 * @param event  事件类型（见 usbpd_dpm_event_e）
 * @param value0  事件关联值 0（CONTRACT=电压 mV）
 * @param value1  事件关联值 1（CONTRACT=电流 mA）
 */
static void sink_dpm_event(uint8_t event, uint32_t value0, uint32_t value1)
{
    if (g_sink.config.dpm.event != 0)
        g_sink.config.dpm.event(0U, event, value0, value1, g_sink.config.dpm.context);
}

/**
 * @brief  DPM 消息接收回调包装（向应用层转发消息，未消费则协议层默认处理）
 *
 * @param msg  协议层消息
 * @param category  消息分类（见 usbpd_dpm_message_category_e）
 * @return USBPD_OK 已消费；USBPD_ERR_UNSUPPORTED 未消费
 */
static int sink_dpm_message(const struct usbpd_protocol_msg_t *msg, uint8_t category)
{
    if ((msg == 0) || (g_sink.config.dpm.message_received == 0))
        return USBPD_ERR_UNSUPPORTED;
    return g_sink.config.dpm.message_received(0U, msg->sop, category, msg->header.bits.type, msg->payload, msg->length,
                                              g_sink.config.dpm.context);
}

/**
 * @brief  准备扩展消息回复（向 DPM 请求填充，失败则 Not_Supported）
 * @param type  扩展消息类型（见 usbpd_extended_e）
 */
static void sink_prepare_extended_reply(uint8_t type)
{
    uint16_t length = sizeof(g_sink.pending_ext_data);
    if ((g_sink.config.dpm.get_extended != 0) &&
        (g_sink.config.dpm.get_extended(0U, type, g_sink.pending_ext_data, &length, g_sink.config.dpm.context) ==
         USBPD_OK) &&
        (length != 0U) && (length <= sizeof(g_sink.pending_ext_data)))
    {
        g_sink.pending_ext_type = type;
        g_sink.pending_ext_length = length;
        g_sink.pending_reply = PD_REPLY_EXTENDED;
    }
    else
    {
        g_sink.pending_reply = PD_REPLY_NOT_SUPPORTED;
    }
}

/**
 * @brief  设置状态机当前状态与截止时间
 *
 * @param state  目标状态（见 pd_sink_state_e）
 * @param deadline  截止时间（毫秒，0=无截止）
 */
static void sink_set_state(uint8_t state, uint32_t deadline)
{
    g_sink.status.state = state;
    g_sink.deadline_ms = deadline;
}

/**
 * @brief  复位电源合约（丢失协商电压/电流、清除 EPR 与 PPS 状态）
 */
static void sink_reset_contract(void)
{
    g_sink.status.contract_valid = 0U;
    g_sink.status.negotiated_voltage_mv = 0U;
    g_sink.status.negotiated_current_ma = 0U;
    g_sink.status.epr_active = 0U;
    g_sink.select_kind = PD_SELECT_NONE;
    g_sink.selected_index = 0U;
    g_sink.epr_caps_valid = 0U;
    g_sink.epr_mode_sent = 0U;
    g_sink.epr_enter_acked = 0U;
    g_sink.epr_keepalive_pending = 0U;
    g_sink.epr_target_pending = 0U;
    g_sink.epr_exit_pending = 0U;
    g_sink.soft_reset_sent = 0U;
    g_sink.wait_pending = 0U;
}

/**
 * @brief  填充 Sink 默认配置（5V/1A Fixed PDO）
 * @param config  输出配置
 */
static void sink_default_config(struct pd_sink_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->max_voltage_mv = 5000U;
    config->max_current_ma = 1000U;
    config->max_power_mw = 5000U;
    config->sink_pdo_count = 1U;
    config->sink_pdo[0].raw = 0U;
    config->sink_pdo[0].sink_fixed.current_10ma = 100U;
    config->sink_pdo[0].sink_fixed.voltage_50mv = 100U;
    config->sink_pdo[0].sink_fixed.type = USBPD_PDO_FIXED;
}

/**
 * @brief  校验 Sink 配置合法性
 *
 * 校验点：max 值非零、PDO 数量范围、首 PDO 为 5V Fixed、EPR PDO 数量
 * 必须 8..11、Sink_Cap_Ext 三元 PDP 单调递增。
 *
 * @param config  待校验配置
 * @return 1=合法，0=非法
 */
static uint8_t sink_config_valid(const struct pd_sink_config_t *config)
{
    if ((config->max_voltage_mv == 0U) || (config->max_current_ma == 0U) || (config->max_power_mw == 0U) ||
        (config->sink_pdo_count == 0U) || (config->sink_pdo_count > USBPD_MAX_DATA_OBJ) ||
        (config->epr_sink_pdo_count > USBPD_MAX_EPR_DATA_OBJ))
        return 0U;
    if ((config->sink_pdo[0].common.type != USBPD_PDO_FIXED) || (config->sink_pdo[0].sink_fixed.voltage_50mv != 100U))
        return 0U;
    if ((config->features.epr != 0U) &&
        ((config->epr_sink_pdo_count <= USBPD_MAX_DATA_OBJ) || (config->epr_sink_pdo_count > USBPD_MAX_EPR_DATA_OBJ)))
        return 0U;
    if ((config->sink_cap_ext_valid != 0U) && ((config->sink_cap_ext.skedb_version != 1U) ||
                                               (config->sink_cap_ext.spr_sink_minimum_pdp.bits.watts >
                                                config->sink_cap_ext.spr_sink_operational_pdp.bits.watts) ||
                                               (config->sink_cap_ext.spr_sink_operational_pdp.bits.watts >
                                                config->sink_cap_ext.spr_sink_maximum_pdp.bits.watts)))
        return 0U;
    if ((config->features.epr != 0U) && (config->sink_cap_ext_valid != 0U) &&
        ((config->sink_cap_ext.epr_sink_operational_pdp_w == 0U) ||
         (config->sink_cap_ext.epr_sink_minimum_pdp_w > config->sink_cap_ext.epr_sink_operational_pdp_w) ||
         (config->sink_cap_ext.epr_sink_operational_pdp_w > config->sink_cap_ext.epr_sink_maximum_pdp_w)))
        return 0U;
    return 1U;
}

/**
 * @brief  判断给定 PDO 列表中是否存在满足目标电压电流的条目
 *
 * 覆盖 Fixed / Variable / Battery / PPS / SPR AVS / EPR AVS 六类 PDO 的可行性校验。
 *
 * @param pdo  PDO 数组
 * @param count  PDO 数量
 * @param voltage_mv  目标电压（mV）
 * @param current_ma  目标电流（mA）
 * @param epr  1=EPR 模式（允许 EPR AVS），0=SPR 模式
 * @return 1=至少一条 PDO 可行，0=均不可行
 */
static uint8_t sink_pdo_allows_target(const union usbpd_pdo_u *pdo, uint8_t count, uint32_t voltage_mv,
                                      uint32_t current_ma, uint8_t epr)
{
    uint8_t i;

    for (i = 0U; i < count; ++i)
    {
        if (pdo[i].common.type == USBPD_PDO_FIXED)
        {
            if ((voltage_mv == pdo[i].fixed.voltage_50mv * 50U) && (current_ma <= pdo[i].fixed.current_10ma * 10U))
            {
                return 1U;
            }
        }
        else if (pdo[i].common.type == USBPD_PDO_VARIABLE)
        {
            if ((voltage_mv >= pdo[i].variable.min_voltage_50mv * 50U) &&
                (voltage_mv <= pdo[i].variable.max_voltage_50mv * 50U) &&
                (current_ma <= pdo[i].variable.current_10ma * 10U))
                return 1U;
        }
        else if (pdo[i].common.type == USBPD_PDO_BATTERY)
        {
            if ((voltage_mv >= pdo[i].battery.min_voltage_50mv * 50U) &&
                (voltage_mv <= pdo[i].battery.max_voltage_50mv * 50U) &&
                (((uint64_t)voltage_mv * current_ma) <= (uint64_t)pdo[i].battery.power_250mw * 250000U))
                return 1U;
        }
        else if ((pdo[i].common.type == USBPD_PDO_APDO) && (pdo[i].pps.subtype == USBPD_APDO_PPS) &&
                 (g_sink.config.features.pps != 0U))
        {
            if ((voltage_mv >= pdo[i].pps.min_voltage_100mv * 100U) &&
                (voltage_mv <= pdo[i].pps.max_voltage_100mv * 100U) && (current_ma <= pdo[i].pps.current_50ma * 50U))
            {
                return 1U;
            }
        }
        else if ((pdo[i].common.type == USBPD_PDO_APDO) && (pdo[i].spr_avs.subtype == USBPD_APDO_SPR_AVS) &&
                 (epr == 0U) && (g_sink.config.features.avs != 0U) && (voltage_mv >= 9000U) && (voltage_mv <= 20000U))
        {
            uint32_t max_ma =
                ((voltage_mv <= 15000U) ? pdo[i].sink_spr_avs.current_15v_10ma : pdo[i].sink_spr_avs.current_20v_10ma) *
                10U;
            if (current_ma <= max_ma)
                return 1U;
        }
        else if ((pdo[i].common.type == USBPD_PDO_APDO) && (pdo[i].avs.subtype == USBPD_APDO_EPR_AVS) && (epr != 0U))
        {
            if ((voltage_mv >= pdo[i].avs.min_voltage_100mv * 100U) &&
                (voltage_mv <= pdo[i].avs.max_voltage_100mv * 100U) &&
                (current_ma <= (uint32_t)(((uint64_t)pdo[i].avs.pdp_w * 1000000U) / voltage_mv)))
            {
                return 1U;
            }
        }
    }

    return 0U;
}

/**
 * @brief  校验应用层请求目标是否在 Sink 能力范围内
 *
 * 先检查全局 max_voltage/max_current/max_power，再根据电压范围选择
 * SPR sink_pdo 列表或 EPR epr_sink_pdo 列表做可行性校验。
 *
 * @param voltage_mv  目标电压（mV）
 * @param current_ma  目标电流（mA）
 * @return 1=允许，0=超出能力
 */
static uint8_t sink_target_allowed(uint32_t voltage_mv, uint32_t current_ma)
{
    uint64_t power = (uint64_t)voltage_mv * current_ma;
    if ((voltage_mv == 0U) || (current_ma == 0U) || (voltage_mv > g_sink.config.max_voltage_mv) ||
        (current_ma > g_sink.config.max_current_ma) || (power > (uint64_t)g_sink.config.max_power_mw * 1000U))
        return 0U;
    if (voltage_mv > 20000U)
    {
        if (g_sink.config.features.epr == 0U)
            return 0U;
        return sink_pdo_allows_target(g_sink.config.epr_sink_pdo, g_sink.config.epr_sink_pdo_count, voltage_mv,
                                      current_ma, 1U);
    }
    return sink_pdo_allows_target(g_sink.config.sink_pdo, g_sink.config.sink_pdo_count, voltage_mv, current_ma, 0U);
}

/**
 * @brief  通过协议层发送数据消息（固定使用 SOP 通道）
 * @param type  数据消息类型
 * @param objects  数据对象数组
 * @param count  对象数量
 * @param now_ms  当前时间
 */
static int sink_send_data(uint8_t type, const uint32_t *objects, uint8_t count, uint32_t now_ms)
{
    return usbpd_protocol_send_data(USBPD_SOP, type, objects, count, now_ms);
}

/**
 * @brief  构造并发送 Request 控制消息
 *
 * 根据 select_kind 选择 RDO 构造分支（Fixed/Battery/PPS/AVS），
 * EPR 模式发送 EPR_Request，否则发送 Request。发送成功后转入 REQUESTED 状态。
 *
 * @param now_ms  当前时间
 * @return 协议层返回码
 */
static int sink_send_request(uint32_t now_ms)
{
    union usbpd_rdo_u rdo;
    int ret;
    memset(&rdo, 0, sizeof(rdo));
    if ((g_sink.select_kind == PD_SELECT_FIXED) || (g_sink.select_kind == PD_SELECT_VARIABLE))
    {
        rdo.fixed.max_current_10ma = (uint16_t)(g_sink.selected_current_ma / 10U);
        rdo.fixed.operating_current_10ma = (uint16_t)(g_sink.selected_current_ma / 10U);
        rdo.fixed.epr_capable = (g_sink.config.features.epr != 0U);
        rdo.fixed.unchunked = 0U;
        rdo.fixed.no_suspend = 1U;
        rdo.fixed.usb_comm = g_sink.config.features.usb_communications;
        rdo.fixed.object_position = (uint8_t)(g_sink.selected_index + 1U);
    }
    else if (g_sink.select_kind == PD_SELECT_BATTERY)
    {
        uint32_t power_mw = (uint32_t)(((uint64_t)g_sink.selected_voltage_mv * g_sink.selected_current_ma) / 1000U);
        rdo.battery.max_power_250mw = (uint16_t)((power_mw + 249U) / 250U);
        rdo.battery.operating_power_250mw = (uint16_t)((power_mw + 249U) / 250U);
        rdo.battery.epr_capable = (g_sink.config.features.epr != 0U);
        rdo.battery.no_suspend = 1U;
        rdo.battery.usb_comm = g_sink.config.features.usb_communications;
        rdo.battery.object_position = (uint8_t)(g_sink.selected_index + 1U);
    }
    else if (g_sink.select_kind == PD_SELECT_PPS)
    {
        rdo.pps.operating_current_50ma = (uint8_t)(g_sink.selected_current_ma / 50U);
        rdo.pps.output_voltage_20mv = (uint16_t)(g_sink.selected_voltage_mv / 20U);
        rdo.pps.epr_capable = (g_sink.config.features.epr != 0U);
        rdo.pps.unchunked = 0U;
        rdo.pps.no_suspend = 1U;
        rdo.pps.usb_comm = g_sink.config.features.usb_communications;
        rdo.pps.object_position = (uint8_t)(g_sink.selected_index + 1U);
    }
    else if (g_sink.select_kind == PD_SELECT_AVS)
    {
        rdo.avs.operating_current_50ma = (uint8_t)(g_sink.selected_current_ma / 50U);
        rdo.avs.output_voltage_25mv = (uint16_t)(g_sink.selected_voltage_mv / 25U);
        rdo.avs.epr_capable = (g_sink.config.features.epr != 0U);
        rdo.avs.unchunked = 0U;
        rdo.avs.no_suspend = 1U;
        rdo.avs.usb_comm = g_sink.config.features.usb_communications;
        rdo.avs.object_position = (uint8_t)(g_sink.selected_index + 1U);
    }
    else
        return USBPD_ERR;
    ret = sink_send_data((g_sink.status.epr_active != 0U) ? USBPD_DATA_EPR_REQUEST : USBPD_DATA_REQUEST, &rdo.raw, 1U,
                         now_ms);
    if (ret == USBPD_OK)
        sink_set_state(PD_SINK_REQUESTED, now_ms + PD_SINK_SENDER_RESPONSE_MS);
    return ret;
}

/**
 * @brief  调度待回复消息的实际发送（在主循环中被周期性调用）
 *
 * 根据 pending_reply 类型选择发送接口（sink_send_data / usbpd_protocol_send_ctrl /
 * usbpd_protocol_send_extended），发送成功后清空调度标志。
 *
 * @param now_ms  当前时间
 */
static void sink_service_reply(uint32_t now_ms)
{
    int ret = USBPD_BUSY;
    switch (g_sink.pending_reply)
    {
    case PD_REPLY_SINK_CAP:
        if (g_sink.config.sink_pdo_count != 0U)
        {
            ret = sink_send_data(USBPD_DATA_SINK_CAP, (const uint32_t *)g_sink.config.sink_pdo,
                                 g_sink.config.sink_pdo_count, now_ms);
        }
        else
            ret = USBPD_ERR;
        break;
    case PD_REPLY_EPR_SINK_CAP:
        if (g_sink.config.epr_sink_pdo_count != 0U)
        {
            ret = usbpd_protocol_send_extended(USBPD_EXT_EPR_SINK_CAP, (const uint8_t *)g_sink.config.epr_sink_pdo,
                                               (uint16_t)g_sink.config.epr_sink_pdo_count * 4U, now_ms);
        }
        else
        {
            g_sink.pending_reply = PD_REPLY_NOT_SUPPORTED;
            return;
        }
        break;
    case PD_REPLY_SINK_CAP_EXT:
        if (g_sink.config.sink_cap_ext_valid != 0U)
        {
            ret = usbpd_protocol_send_extended(USBPD_EXT_SINK_CAP, (const uint8_t *)&g_sink.config.sink_cap_ext,
                                               sizeof(g_sink.config.sink_cap_ext), now_ms);
        }
        else
        {
            g_sink.pending_reply = PD_REPLY_NOT_SUPPORTED;
            return;
        }
        break;
    case PD_REPLY_REVISION: {
        union usbpd_revision_do_u revision;
        revision.raw = 0U;
        revision.bits.revision_major = 3U;
        revision.bits.revision_minor = 2U;
        revision.bits.version_major = 1U;
        revision.bits.version_minor = 1U;
        ret = sink_send_data(USBPD_DATA_REVISION, &revision.raw, 1U, now_ms);
        break;
    }
    case PD_REPLY_EXTENDED:
        ret = usbpd_protocol_send_extended(g_sink.pending_ext_type, g_sink.pending_ext_data, g_sink.pending_ext_length,
                                           now_ms);
        break;
    case PD_REPLY_REJECT:
        ret = usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_REJECT, now_ms);
        break;
    case PD_REPLY_VDM_NAK:
        ret = usbpd_protocol_send_data(g_sink.pending_vdm_sop, USBPD_DATA_VENDOR_DEFINED, &g_sink.pending_vdm, 1U,
                                       now_ms);
        break;
    case PD_REPLY_NOT_SUPPORTED:
        ret = usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_NOT_SUPPORTED, now_ms);
        break;
    default:
        return;
    }
    if (ret == USBPD_OK)
        g_sink.pending_reply = PD_REPLY_NONE;
}

/**
 * @brief  在给定 PDO 列表中查找满足应用层请求的条目并选中
 *
 * 覆盖 Fixed / Variable / Battery / PPS / SPR AVS / EPR AVS 六类 PDO；
 * APDO 电压电流对齐到协议步长（20mV / 50mA / 25mV）。
 *
 * @param pdo  PDO 数组
 * @param count  PDO 数量
 * @param epr  1=EPR 模式（允许 EPR AVS），0=SPR 模式
 * @return USBPD_OK 选中成功；USBPD_ERR 未找到匹配
 */
static int sink_choose_from(const union usbpd_pdo_u *pdo, uint8_t count, uint8_t epr)
{
    uint8_t i;
    for (i = 0U; i < count; ++i)
    {
        uint32_t voltage;
        uint32_t current;
        if (pdo[i].common.type == USBPD_PDO_FIXED)
        {
            voltage = pdo[i].fixed.voltage_50mv * 50U;
            current = pdo[i].fixed.current_10ma * 10U;
            if ((voltage == g_sink.status.requested_voltage_mv) && (current >= g_sink.status.requested_current_ma))
            {
                g_sink.select_kind = PD_SELECT_FIXED;
                g_sink.selected_index = i;
                g_sink.selected_voltage_mv = voltage;
                g_sink.selected_current_ma = g_sink.status.requested_current_ma;
                return USBPD_OK;
            }
        }
        else if (pdo[i].common.type == USBPD_PDO_VARIABLE)
        {
            uint32_t min_mv = pdo[i].variable.min_voltage_50mv * 50U;
            uint32_t max_mv = pdo[i].variable.max_voltage_50mv * 50U;
            current = pdo[i].variable.current_10ma * 10U;
            if ((g_sink.status.requested_voltage_mv >= min_mv) && (g_sink.status.requested_voltage_mv <= max_mv) &&
                (current >= g_sink.status.requested_current_ma))
            {
                g_sink.select_kind = PD_SELECT_VARIABLE;
                g_sink.selected_index = i;
                g_sink.selected_voltage_mv = g_sink.status.requested_voltage_mv;
                g_sink.selected_current_ma = g_sink.status.requested_current_ma;
                return USBPD_OK;
            }
        }
        else if (pdo[i].common.type == USBPD_PDO_BATTERY)
        {
            uint32_t min_mv = pdo[i].battery.min_voltage_50mv * 50U;
            uint32_t max_mv = pdo[i].battery.max_voltage_50mv * 50U;
            uint32_t max_mw = pdo[i].battery.power_250mw * 250U;
            uint32_t request_mw =
                (uint32_t)(((uint64_t)g_sink.status.requested_voltage_mv * g_sink.status.requested_current_ma) / 1000U);
            if ((g_sink.status.requested_voltage_mv >= min_mv) && (g_sink.status.requested_voltage_mv <= max_mv) &&
                (request_mw <= max_mw))
            {
                g_sink.select_kind = PD_SELECT_BATTERY;
                g_sink.selected_index = i;
                g_sink.selected_voltage_mv = g_sink.status.requested_voltage_mv;
                g_sink.selected_current_ma = g_sink.status.requested_current_ma;
                return USBPD_OK;
            }
        }
        else if ((pdo[i].common.type == USBPD_PDO_APDO) && (pdo[i].pps.subtype == USBPD_APDO_PPS) &&
                 (g_sink.config.features.pps != 0U))
        {
            uint32_t min_mv = pdo[i].pps.min_voltage_100mv * 100U;
            uint32_t max_mv = pdo[i].pps.max_voltage_100mv * 100U;
            current = pdo[i].pps.current_50ma * 50U;
            if ((g_sink.status.requested_voltage_mv >= min_mv) && (g_sink.status.requested_voltage_mv <= max_mv) &&
                (current >= g_sink.status.requested_current_ma))
            {
                g_sink.select_kind = PD_SELECT_PPS;
                g_sink.selected_index = i;
                g_sink.selected_voltage_mv = (g_sink.status.requested_voltage_mv / 20U) * 20U;
                g_sink.selected_current_ma = (g_sink.status.requested_current_ma / 50U) * 50U;
                return USBPD_OK;
            }
        }
        else if ((pdo[i].common.type == USBPD_PDO_APDO) && (pdo[i].spr_avs.subtype == USBPD_APDO_SPR_AVS) &&
                 (g_sink.config.features.avs != 0U) && (g_sink.status.requested_voltage_mv >= 9000U) &&
                 (g_sink.status.requested_voltage_mv <= 20000U))
        {
            current = ((g_sink.status.requested_voltage_mv <= 15000U) ? pdo[i].spr_avs.current_15v_10ma
                                                                      : pdo[i].spr_avs.current_20v_10ma) *
                      10U;
            if (current >= g_sink.status.requested_current_ma)
            {
                g_sink.select_kind = PD_SELECT_AVS;
                g_sink.selected_index = i;
                g_sink.selected_voltage_mv = (g_sink.status.requested_voltage_mv / 25U) * 25U;
                g_sink.selected_current_ma = (g_sink.status.requested_current_ma / 50U) * 50U;
                return USBPD_OK;
            }
        }
        else if ((pdo[i].common.type == USBPD_PDO_APDO) && (pdo[i].avs.subtype == USBPD_APDO_EPR_AVS) && (epr != 0U))
        {
            uint32_t min_mv = pdo[i].avs.min_voltage_100mv * 100U;
            uint32_t max_mv = pdo[i].avs.max_voltage_100mv * 100U;
            current = sink_min(g_sink.config.max_current_ma, (uint32_t)(((uint64_t)pdo[i].avs.pdp_w * 1000000U) /
                                                                        g_sink.status.requested_voltage_mv));
            if ((g_sink.status.requested_voltage_mv >= min_mv) && (g_sink.status.requested_voltage_mv <= max_mv) &&
                (current >= g_sink.status.requested_current_ma))
            {
                g_sink.select_kind = PD_SELECT_AVS;
                g_sink.selected_index = i;
                g_sink.selected_voltage_mv = (g_sink.status.requested_voltage_mv / 25U) * 25U;
                g_sink.selected_current_ma = (g_sink.status.requested_current_ma / 50U) * 50U;
                return USBPD_OK;
            }
        }
    }
    return USBPD_ERR;
}

/**
 * @brief  在 Source PDO 列表中查找 5V Fixed PDO 作为兜底
 *
 * @return USBPD_OK 找到并选中；USBPD_ERR 未找到
 */
static int sink_choose_5v(void)
{
    uint8_t i;
    for (i = 0U; i < g_sink.source_count; ++i)
    {
        if ((g_sink.source_pdo[i].common.type == USBPD_PDO_FIXED) && (g_sink.source_pdo[i].fixed.voltage_50mv == 100U))
        {
            g_sink.select_kind = PD_SELECT_FIXED;
            g_sink.selected_index = i;
            g_sink.selected_voltage_mv = 5000U;
            g_sink.selected_current_ma =
                sink_min(g_sink.config.max_current_ma,
                         sink_min(g_sink.status.requested_current_ma, g_sink.source_pdo[i].fixed.current_10ma * 10U));
            g_sink.status.fallback_active = 1U;
            return (g_sink.selected_current_ma != 0U) ? USBPD_OK : USBPD_ERR;
        }
    }
    return USBPD_ERR;
}

/**
 * @brief  开始 PDO 选择流程（根据请求电压范围决定 EPR 进入、EPR 能力查询或直接选 PDO）
 *
 * @param now_ms  当前时间
 */
static void sink_begin_selection(uint32_t now_ms)
{
    g_sink.status.fallback_active = 0U;
    if (g_sink.status.requested_voltage_mv > 20000U)
    {
        if (g_sink.status.contract_valid == 0U)
        {
            /* EPR entry is only valid from an existing SPR Explicit Contract. */
            g_sink.epr_target_pending = 1U;
            if (sink_choose_5v() == USBPD_OK)
                (void)sink_send_request(now_ms);
            else
                sink_set_state(PD_SINK_ERROR_RECOVERY, now_ms + PD_SINK_ERROR_RECOVERY_MS);
            return;
        }
        if (g_sink.status.epr_active == 0U)
        {
            if ((g_sink.config.features.epr == 0U) || (g_sink.source_count == 0U) ||
                (g_sink.source_pdo[0].fixed.epr_capable == 0U))
            {
                if (sink_choose_5v() == USBPD_OK)
                    (void)sink_send_request(now_ms);
                return;
            }
            g_sink.epr_mode_sent = 0U;
            g_sink.epr_enter_acked = 0U;
            sink_set_state(PD_SINK_EPR_ENTER, now_ms + USBPD_T_ENTER_EPR_MS);
            return;
        }
        if (g_sink.epr_caps_valid == 0U)
        {
            g_sink.get_epr_cap_sent = 0U;
            sink_set_state(PD_SINK_EPR_CAPS, now_ms + PD_SINK_SENDER_RESPONSE_MS);
            return;
        }
        if (sink_choose_from(g_sink.epr_source_pdo, g_sink.epr_source_count, 1U) == USBPD_OK)
        {
            (void)sink_send_request(now_ms);
            return;
        }
    }
    else if (g_sink.status.epr_active != 0U)
    {
        /* Renegotiate to an SPR (A)PDO with EPR_Request before sending Exit. */
        if (sink_choose_from(g_sink.epr_source_pdo, g_sink.epr_source_count, 1U) == USBPD_OK)
        {
            g_sink.epr_exit_pending = 1U;
            (void)sink_send_request(now_ms);
            return;
        }
    }
    else if (sink_choose_from(g_sink.source_pdo, g_sink.source_count, 0U) == USBPD_OK)
    {
        (void)sink_send_request(now_ms);
        return;
    }
    if (sink_choose_5v() == USBPD_OK)
    {
        if (g_sink.status.epr_active != 0U)
            g_sink.epr_exit_pending = 1U;
        (void)sink_send_request(now_ms);
    }
    else
        sink_set_state(PD_SINK_ERROR_RECOVERY, now_ms + PD_SINK_ERROR_RECOVERY_MS);
}

/**
 * @brief  处理接收到的 Source_Capabilities 消息
 *
 * 缓存 PDO 列表、重置等待重试计数；若当前处于 Discovery 或 Ready 状态，
 * 立即触发 sink_begin_selection()。
 *
 * @param msg  协议层消息（应为 Source_Capabilities）
 * @param now_ms  当前时间
 */
static void sink_handle_source_caps(const struct usbpd_protocol_msg_t *msg, uint32_t now_ms)
{
    uint8_t count;
    if ((msg == 0) || (msg->length == 0U) || (msg->length > USBPD_MAX_DATA_OBJ * 4U))
        return;
    count = (uint8_t)(msg->length / 4U);
    memcpy(g_sink.source_pdo, msg->payload, msg->length);
    g_sink.source_count = count;
    g_sink.status.source_pdo_count = count;
    g_sink.source_valid = 1U;
    g_sink.wait_retries = 0U;
    if ((g_sink.status.state == PD_SINK_DISCOVERY) || (g_sink.status.state == PD_SINK_READY))
        sink_begin_selection(now_ms);
}

/**
 * @brief  校验 EPR Source_Capabilities 中首个 PDO 是否为 EPR 标志的 5V Fixed
 *
 * @param payload  EPR Source PDO 列表原始字节
 * @param count  PDO 数量
 * @return 1=合法，0=非法
 */
static uint8_t sink_epr_caps_are_valid(const uint8_t *payload, uint8_t count)
{
    union usbpd_pdo_u first;
    if ((payload == 0) || (count < USBPD_MAX_DATA_OBJ) || (count > USBPD_MAX_EPR_DATA_OBJ))
        return 0U;
    memcpy(first.bytes, payload, sizeof(first.bytes));
    return ((first.common.type == USBPD_PDO_FIXED) && (first.fixed.voltage_50mv == 100U) &&
            (first.fixed.epr_capable != 0U));
}

/**
 * @brief  协议层事件回调（Sink 策略层与协议层的主要交互入口）
 *
 * 处理 Hard Reset / Soft Reset / TX_TIMEOUT / RX_OVERFLOW / ERROR 等协议事件，
 * 以及 RX / EXT_RX 两类消息事件的分发。
 *
 * @param event  协议层事件类型（见 usbpd_protocol_event_e）
 * @param msg  事件关联消息（RX / EXT_RX 事件携带）
 * @param arg  协议层回调用户参数（未使用，固定为 0）
 */
static void sink_protocol_event(uint8_t event, const struct usbpd_protocol_msg_t *msg, void *arg)
{
    uint32_t now_ms = g_sink.now_ms;
    (void)arg;
    if (event == USBPD_PROTOCOL_HARD_RESET)
    {
        sink_dpm_event(USBPD_DPM_HARD_RESET, 0U, 0U);
        sink_reset_contract();
        usbpd_protocol_reset();
        sink_set_state(PD_SINK_ERROR_RECOVERY, now_ms + PD_SINK_ERROR_RECOVERY_MS);
        return;
    }
    if (event == USBPD_PROTOCOL_SOFT_RESET)
    {
        usbpd_protocol_reset();
        g_sink.peer_soft_reset = 1U;
        g_sink.soft_reset_sent = 0U;
        sink_set_state(PD_SINK_SOFT_RESET, now_ms + PD_SINK_SENDER_RESPONSE_MS);
        return;
    }
    if ((event == USBPD_PROTOCOL_TX_TIMEOUT) || (event == USBPD_PROTOCOL_RX_OVERFLOW) ||
        (event == USBPD_PROTOCOL_ERROR))
    {
        usbpd_observer_record((event == USBPD_PROTOCOL_TX_TIMEOUT) ? MESSAGE_BUFFER_TX : MESSAGE_BUFFER_RX,
                              USBPD_SOP_INVALID, 0, 0U, now_ms, USBPD_OBSERVER_ERROR);
        sink_dpm_event(USBPD_DPM_PROTOCOL_ERROR, event, 0U);
        sink_reset_contract();
        sink_set_state(PD_SINK_HARD_RESET, now_ms + PD_SINK_SENDER_RESPONSE_MS);
        return;
    }
    if (event == USBPD_PROTOCOL_EXT_RX)
    {
        if ((msg != 0) && (msg->header.bits.type == USBPD_EXT_EPR_SOURCE_CAP) && ((msg->length % 4U) == 0U))
        {
            g_sink.epr_source_count = (uint8_t)(msg->length / 4U);
            if (g_sink.epr_source_count > USBPD_MAX_EPR_DATA_OBJ)
                g_sink.epr_source_count = USBPD_MAX_EPR_DATA_OBJ;
            if (sink_epr_caps_are_valid(msg->payload, g_sink.epr_source_count) == 0U)
            {
                sink_set_state(PD_SINK_SOFT_RESET, now_ms);
                return;
            }
            memcpy(g_sink.epr_source_pdo, msg->payload, g_sink.epr_source_count * 4U);
            g_sink.epr_caps_valid = 1U;
            g_sink.get_epr_cap_sent = 0U;
            if ((g_sink.status.epr_active != 0U) && (g_sink.status.state == PD_SINK_EPR_CAPS))
                sink_begin_selection(now_ms);
        }
        else if ((msg != 0) && (msg->header.bits.type == USBPD_EXT_CONTROL) &&
                 (msg->length == sizeof(struct usbpd_extended_control_db_t)))
        {
            const struct usbpd_extended_control_db_t *ecdb = (const struct usbpd_extended_control_db_t *)msg->payload;
            if ((ecdb->type == USBPD_EXT_CTRL_EPR_KEEPALIVE_ACK) && (g_sink.epr_keepalive_pending != 0U))
            {
                g_sink.epr_keepalive_pending = 0U;
                g_sink.epr_keepalive_ms = now_ms + PD_SINK_EPR_KEEPALIVE_MS;
                sink_set_state(PD_SINK_READY, 0U);
            }
            else if (ecdb->type == USBPD_EXT_CTRL_EPR_GET_SINK_CAP)
            {
                g_sink.pending_reply =
                    (g_sink.config.features.epr != 0U) ? PD_REPLY_EPR_SINK_CAP : PD_REPLY_NOT_SUPPORTED;
            }
            else
            {
                g_sink.pending_reply = PD_REPLY_NOT_SUPPORTED;
            }
        }
        else if ((msg != 0) && (sink_dpm_message(msg, USBPD_DPM_EXTENDED_MESSAGE) != USBPD_OK))
        {
            switch (msg->header.bits.type)
            {
            case USBPD_EXT_GET_BATTERY_CAP:
            case USBPD_EXT_GET_BATTERY_STATUS:
            case USBPD_EXT_GET_MANUFACTURER_INFO:
            case USBPD_EXT_SECURITY_REQUEST:
            case USBPD_EXT_FIRMWARE_UPDATE_REQUEST:
                g_sink.pending_reply = PD_REPLY_NOT_SUPPORTED;
                break;
            default:
                break;
            }
        }
        return;
    }
    if ((event != USBPD_PROTOCOL_RX) || (msg == 0))
        return;
    if (msg->header.bits.data_objects != 0U)
    {
        if ((msg->sop == USBPD_SOP) && (msg->header.bits.type == USBPD_DATA_SOURCE_CAP))
        {
            sink_handle_source_caps(msg, now_ms);
        }
        else if ((msg->sop == USBPD_SOP) && (msg->header.bits.type == USBPD_DATA_EPR_MODE) &&
                 (msg->length >= sizeof(union usbpd_epr_mode_do_u)))
        {
            union usbpd_epr_mode_do_u mode;
            memcpy(&mode.raw, msg->payload, sizeof(mode.raw));
            if ((g_sink.status.state == PD_SINK_EPR_ENTER) && (mode.bits.action == USBPD_EPR_MODE_ENTER_ACK))
            {
                g_sink.epr_enter_acked = 1U;
                g_sink.deadline_ms = now_ms + USBPD_T_ENTER_EPR_MS;
            }
            else if ((g_sink.status.state == PD_SINK_EPR_ENTER) &&
                     (mode.bits.action == USBPD_EPR_MODE_ENTER_SUCCEEDED) && (g_sink.epr_enter_acked != 0U))
            {
                g_sink.status.epr_active = 1U;
                sink_dpm_event(USBPD_DPM_EPR_ENTERED, 0U, 0U);
                g_sink.epr_caps_valid = 0U;
                g_sink.get_epr_cap_sent = 0U;
                sink_set_state(PD_SINK_EPR_CAPS, now_ms + PD_SINK_SENDER_RESPONSE_MS);
            }
            else if ((g_sink.status.state == PD_SINK_EPR_ENTER) && (mode.bits.action == USBPD_EPR_MODE_ENTER_FAILED))
            {
                g_sink.epr_target_pending = 0U;
                g_sink.status.fallback_active = 1U;
                sink_set_state(PD_SINK_READY, 0U);
            }
            else if (mode.bits.action == USBPD_EPR_MODE_EXIT)
            {
                g_sink.status.epr_active = 0U;
                sink_dpm_event(USBPD_DPM_EPR_EXITED, 0U, 0U);
                g_sink.epr_caps_valid = 0U;
                if (g_sink.status.negotiated_voltage_mv > 20000U)
                    sink_set_state(PD_SINK_HARD_RESET, now_ms);
                else
                    sink_set_state(PD_SINK_READY, 0U);
            }
            else
            {
                sink_set_state(PD_SINK_SOFT_RESET, now_ms);
            }
        }
        else if (sink_dpm_message(msg, USBPD_DPM_DATA_MESSAGE) != USBPD_OK)
        {
            if ((msg->header.bits.type == USBPD_DATA_VENDOR_DEFINED) &&
                (msg->length >= sizeof(union usbpd_vdm_header_u)))
            {
                union usbpd_vdm_header_u vdm;
                memcpy(&vdm.raw, msg->payload, sizeof(vdm.raw));
                if ((vdm.structured.structured != 0U) && (vdm.structured.command_type == USBPD_VDM_CMD_REQUEST))
                {
                    vdm.structured.command_type = USBPD_VDM_CMD_NAK;
                    g_sink.pending_vdm = vdm.raw;
                    g_sink.pending_vdm_sop = msg->sop;
                    g_sink.pending_reply = PD_REPLY_VDM_NAK;
                }
            }
            else if (msg->header.bits.type == USBPD_DATA_ENTER_USB)
            {
                g_sink.pending_reply = PD_REPLY_NOT_SUPPORTED;
            }
        }
        return;
    }
    switch (msg->header.bits.type)
    {
    case USBPD_CTRL_ACCEPT:
        if (g_sink.status.state == PD_SINK_REQUESTED)
        {
            sink_set_state(PD_SINK_TRANSITION, now_ms + ((g_sink.status.epr_active != 0U) ? USBPD_T_PS_TRANSITION_EPR_MS
                                                                                          : PD_SINK_PS_TRANSITION_MS));
        }
        else if ((g_sink.status.state == PD_SINK_SOFT_RESET) && (g_sink.soft_reset_sent != 0U))
        {
            usbpd_protocol_reset();
            g_sink.soft_reset_sent = 0U;
            sink_set_state((g_sink.status.contract_valid != 0U) ? PD_SINK_READY : PD_SINK_DISCOVERY,
                           (g_sink.status.contract_valid != 0U) ? 0U : now_ms + PD_SINK_WAIT_CAP_MS);
        }
        break;
    case USBPD_CTRL_PS_RDY:
        if (g_sink.status.state == PD_SINK_TRANSITION)
        {
            g_sink.status.contract_valid = 1U;
            g_sink.status.negotiated_voltage_mv = g_sink.selected_voltage_mv;
            g_sink.status.negotiated_current_ma = g_sink.selected_current_ma;
            g_sink.status.selected_pdo = (uint8_t)(g_sink.selected_index + 1U);
            g_sink.status.fallback_active = 0U;
            g_sink.wait_retries = 0U;
            g_sink.hard_reset_count = 0U;
            if (g_sink.epr_target_pending != 0U)
            {
                g_sink.epr_target_pending = 0U;
                g_sink.target_dirty = 1U;
            }
            else
            {
                g_sink.target_dirty = 0U;
            }
            g_sink.epr_keepalive_ms = now_ms + PD_SINK_EPR_KEEPALIVE_MS;
            g_sink.pps_request_ms = now_ms + (USBPD_T_PPS_REQUEST_MS - 1000U);
            sink_dpm_event(USBPD_DPM_CONTRACT, g_sink.status.negotiated_voltage_mv,
                           g_sink.status.negotiated_current_ma);
            sink_set_state(PD_SINK_READY, 0U);
        }
        break;
    case USBPD_CTRL_REJECT:
        if (g_sink.status.state == PD_SINK_REQUESTED)
        {
            if ((g_sink.select_kind == PD_SELECT_FIXED) && (g_sink.selected_voltage_mv == 5000U))
            {
                sink_set_state(PD_SINK_HARD_RESET, now_ms);
            }
            else if (sink_choose_5v() == USBPD_OK)
            {
                if (g_sink.status.epr_active != 0U)
                    g_sink.epr_exit_pending = 1U;
                (void)sink_send_request(now_ms);
            }
        }
        break;
    case USBPD_CTRL_WAIT:
        if (g_sink.status.state == PD_SINK_REQUESTED)
        {
            if (g_sink.wait_retries++ < PD_SINK_MAX_WAIT_RETRIES)
            {
                g_sink.wait_pending = 1U;
                sink_set_state(PD_SINK_DISCOVERY, now_ms + USBPD_T_SINK_REQUEST_MS);
            }
            else if (sink_choose_5v() == USBPD_OK)
            {
                if (g_sink.status.epr_active != 0U)
                    g_sink.epr_exit_pending = 1U;
                (void)sink_send_request(now_ms);
            }
        }
        break;
    case USBPD_CTRL_NOT_SUPPORTED:
        if (g_sink.epr_keepalive_pending != 0U)
            sink_set_state(PD_SINK_HARD_RESET, now_ms);
        break;
    case USBPD_CTRL_GET_SINK_CAP:
        g_sink.pending_reply = PD_REPLY_SINK_CAP;
        break;
    case USBPD_CTRL_GET_SINK_CAP_EXT:
        g_sink.pending_reply = PD_REPLY_SINK_CAP_EXT;
        break;
    case USBPD_CTRL_GET_STATUS:
        sink_prepare_extended_reply(USBPD_EXT_STATUS);
        break;
    case USBPD_CTRL_GET_COUNTRY_CODES:
        sink_prepare_extended_reply(USBPD_EXT_COUNTRY_CODES);
        break;
    case USBPD_CTRL_GET_REVISION:
        g_sink.pending_reply = PD_REPLY_REVISION;
        break;
    case USBPD_CTRL_DR_SWAP:
    case USBPD_CTRL_PR_SWAP:
    case USBPD_CTRL_VCONN_SWAP:
    case USBPD_CTRL_FR_SWAP:
        g_sink.pending_reply = PD_REPLY_REJECT;
        break;
    case USBPD_CTRL_GOTO_MIN:
    case USBPD_CTRL_PING:
    case USBPD_CTRL_GET_SOURCE_CAP:
    case USBPD_CTRL_DATA_RESET:
    case USBPD_CTRL_GET_SOURCE_CAP_EXT:
    case USBPD_CTRL_GET_PPS_STATUS:
    case USBPD_CTRL_GET_SOURCE_INFO:
        g_sink.pending_reply = PD_REPLY_NOT_SUPPORTED;
        break;
    case USBPD_CTRL_DATA_RESET_COMPLETE:
        break;
    default:
        (void)sink_dpm_message(msg, USBPD_DPM_CONTROL_MESSAGE);
        break;
    }
}

/* ===== 对外接口 ===== */
/**
 * @brief  查询端口是否已连接
 * @return 1=已连接（状态 != Unattached），0=未连接
 */
uint8_t pd_sink_is_attached(void)
{
    return (g_sink.status.state != PD_SINK_UNATTACHED);
}

/**
 * @brief  发起功率请求（仅登记目标，实际发送由状态机在下一机会触发）
 *
 * @param voltage_mv  目标电压（mV）
 * @param current_ma  目标电流（mA）
 * @return USBPD_OK 请求已登记；USBPD_ERR 参数越界
 */
int pd_sink_request(uint32_t voltage_mv, uint32_t current_ma)
{
    if (sink_target_allowed(voltage_mv, current_ma) == 0U)
        return USBPD_ERR;
    g_sink.status.requested_voltage_mv = voltage_mv;
    g_sink.status.requested_current_ma = current_ma;
    g_sink.target_dirty = 1U;
    return USBPD_OK;
}

/**
 * @brief  查询 Sink 运行时状态快照
 * @param status  输出状态结构
 * @return USBPD_OK 成功；USBPD_ERR 参数为 NULL
 */
int pd_sink_get_status(struct pd_sink_status_t *status)
{
    if (status == 0)
        return USBPD_ERR;
    *status = g_sink.status;
    return USBPD_OK;
}

/**
 * @brief  手动发送控制消息（调试/测试用）
 * @param sop  SOP 类型
 * @param type  控制消息类型
 * @return 协议层返回码
 */
int pd_sink_send_control(uint8_t sop, uint8_t type)
{
    if ((sop > USBPD_SOP_DPRIME) || (type > USBPD_CTRL_GET_REVISION))
        return USBPD_ERR_PARAM;
    return usbpd_protocol_send_ctrl(sop, type, g_sink.now_ms);
}

/**
 * @brief  手动发送数据消息（调试/测试用）
 * @param sop  SOP 类型
 * @param type  数据消息类型
 * @param objects  数据对象数组
 * @param count  对象数量
 * @return 协议层返回码
 */
int pd_sink_send_data_objects(uint8_t sop, uint8_t type, const uint32_t *objects, uint8_t count)
{
    if ((sop > USBPD_SOP_DPRIME) || (type > USBPD_DATA_VENDOR_DEFINED))
        return USBPD_ERR_PARAM;
    return usbpd_protocol_send_data(sop, type, objects, count, g_sink.now_ms);
}

/**
 * @brief  手动发送扩展消息（调试/测试用）
 * @param sop  SOP 类型
 * @param type  扩展消息类型
 * @param data  扩展数据缓冲
 * @param length  数据长度
 * @return 协议层返回码
 */
int pd_sink_send_extended(uint8_t sop, uint8_t type, const uint8_t *data, uint16_t length)
{
    if ((sop > USBPD_SOP_DPRIME) || (type > USBPD_EXT_VENDOR_DEFINED))
        return USBPD_ERR_PARAM;
    return usbpd_protocol_send_extended_sop(sop, type, data, length, g_sink.now_ms);
}

/**
 * @brief  初始化 Sink 策略层（完整初始化：配置 + PHY + 协议层）
 *
 * @param config  初始化配置（可为 NULL，此时使用 5V/1A 默认值）
 * @return USBPD_OK 成功；USBPD_ERR_PARAM 配置非法；USBPD_ERR PHY 初始化失败
 */
int pd_sink_init(const struct pd_sink_config_t *config)
{
    return pd_sink_policy_init(config, 1U);
}

/**
 * @brief  初始化 Sink 策略层（可控是否初始化底层硬件）
 *
 * @param config  初始化配置（可为 NULL）
 * @param initialize_hardware  1=同时初始化 PHY 与观察器，0=仅配置策略层
 * @return USBPD_OK 成功；USBPD_ERR_PARAM 配置非法；USBPD_ERR PHY 初始化失败
 */
int pd_sink_policy_init(const struct pd_sink_config_t *config, uint8_t initialize_hardware)
{
    struct pd_sink_config_t defaults;
    memset(&g_sink, 0, sizeof(g_sink));
    sink_default_config(&defaults);
    g_sink.config = (config != 0) ? *config : defaults;
    if (sink_config_valid(&g_sink.config) == 0U)
        return USBPD_ERR_PARAM;
    g_sink.status.requested_voltage_mv = 5000U;
    g_sink.status.requested_current_ma = sink_min(1000U, g_sink.config.max_current_ma);
    usbpd_protocol_init(sink_protocol_event, 0);
    usbpd_protocol_configure(USBPD_REV30, USBPD_POWER_ROLE_SINK, USBPD_DATA_ROLE_UFP);
    if (initialize_hardware != 0U)
    {
        usbpd_observer_init();
        if (usbpd_phy_init() != USBPD_OK)
            return USBPD_ERR;
    }
    usbpd_phy_set_pull(USBPD_CC_PULL_RD);
    sink_set_state(PD_SINK_UNATTACHED, 0U);
    return USBPD_OK;
}

/**
 * @brief  Sink 策略层周期任务（需在主循环中以毫秒节拍调用）
 *
 * 内部依次调用 PHY 层任务、协议层任务、待发送回复调度、CC/VBUS 检测、
 * 状态机迁移、EPR KeepAlive / PPS 动态请求 / Soft-Hard Reset 处理。
 *
 * @param now_ms  当前系统时间（毫秒）
 */
void pd_sink_task(uint32_t now_ms)
{
    enum usbpd_cc_e cc1;
    enum usbpd_cc_e cc2;
    uint32_t vbus;
    g_sink.now_ms = now_ms;
    usbpd_phy_task(now_ms);
    usbpd_protocol_task(now_ms);
    sink_service_reply(now_ms);
    if (usbpd_phy_get_vbus(&vbus) == USBPD_OK)
        g_sink.status.vbus_mv = vbus;
    usbpd_phy_get_cc(&cc1, &cc2);
    if (((g_sink.status.state != PD_SINK_UNATTACHED) && (g_sink.status.state != PD_SINK_ERROR_RECOVERY)) &&
        (!sink_is_rp(cc1) && !sink_is_rp(cc2)))
    {
        if (g_sink.status.contract_valid != 0U)
            sink_dpm_event(USBPD_DPM_CONTRACT_LOST, 0U, 0U);
        sink_dpm_event(USBPD_DPM_DETACHED, 0U, 0U);
        sink_reset_contract();
        g_sink.hard_reset_count = 0U;
        usbpd_protocol_reset();
        g_sink.source_valid = 0U;
        g_sink.epr_caps_valid = 0U;
        sink_set_state(PD_SINK_UNATTACHED, 0U);
    }
    switch (g_sink.status.state)
    {
    case PD_SINK_UNATTACHED:
        if (sink_is_rp(cc1) || sink_is_rp(cc2))
            sink_set_state(PD_SINK_ATTACH_WAIT, now_ms + PD_SINK_CC_DEBOUNCE_MS);
        break;
    case PD_SINK_ATTACH_WAIT:
        if (!sink_is_rp(cc1) && !sink_is_rp(cc2))
            sink_set_state(PD_SINK_UNATTACHED, 0U);
        else if (sink_deadline(now_ms))
        {
            usbpd_phy_set_cc(sink_is_rp(cc1) ? 1U : 2U);
            usbpd_protocol_configure(USBPD_REV30, USBPD_POWER_ROLE_SINK, USBPD_DATA_ROLE_UFP);
            sink_set_state(PD_SINK_DISCOVERY, now_ms + PD_SINK_WAIT_CAP_MS);
            sink_dpm_event(USBPD_DPM_ATTACHED, 0U, 0U);
            if (g_sink.source_valid != 0U)
                sink_begin_selection(now_ms);
        }
        break;
    case PD_SINK_DISCOVERY:
        if ((g_sink.wait_pending != 0U) && !sink_deadline(now_ms))
        {
            break;
        }
        else if (g_sink.source_valid != 0U)
        {
            g_sink.wait_pending = 0U;
            sink_begin_selection(now_ms);
        }
        else if (sink_deadline(now_ms))
        {
            if (g_sink.get_source_cap_sent == 0U)
            {
                if (usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_GET_SOURCE_CAP, now_ms) == USBPD_OK)
                {
                    g_sink.get_source_cap_sent = 1U;
                    g_sink.deadline_ms = now_ms + PD_SINK_WAIT_CAP_MS;
                }
            }
            else
                sink_set_state(PD_SINK_SOFT_RESET, now_ms);
        }
        break;
    case PD_SINK_EPR_CAPS:
        if (g_sink.epr_caps_valid != 0U)
            sink_begin_selection(now_ms);
        else if (sink_deadline(now_ms) && (g_sink.get_epr_cap_sent == 0U) &&
                 (usbpd_protocol_send_extended_control(USBPD_EXT_CTRL_EPR_GET_SOURCE_CAP, 0U, now_ms) == USBPD_OK))
        {
            g_sink.get_epr_cap_sent = 1U;
            g_sink.deadline_ms = now_ms + PD_SINK_SENDER_RESPONSE_MS;
        }
        else if (sink_deadline(now_ms) && (g_sink.get_epr_cap_sent != 0U))
        {
            sink_set_state(PD_SINK_HARD_RESET, now_ms);
        }
        break;
    case PD_SINK_EPR_ENTER:
        if (g_sink.epr_mode_sent == 0U)
        {
            union usbpd_epr_mode_do_u mode;
            uint32_t operational_pdp;
            mode.raw = 0U;
            mode.bits.action = USBPD_EPR_MODE_ENTER;
            operational_pdp = (g_sink.config.sink_cap_ext_valid != 0U)
                                  ? g_sink.config.sink_cap_ext.epr_sink_operational_pdp_w
                                  : (g_sink.config.max_power_mw + 999U) / 1000U;
            mode.bits.data = (uint8_t)sink_min(255U, operational_pdp);
            if (sink_send_data(USBPD_DATA_EPR_MODE, &mode.raw, 1U, now_ms) == USBPD_OK)
            {
                g_sink.epr_mode_sent = 1U;
                g_sink.deadline_ms = now_ms + PD_SINK_SENDER_RESPONSE_MS;
            }
        }
        else if (sink_deadline(now_ms))
        {
            sink_set_state(PD_SINK_SOFT_RESET, now_ms);
        }
        break;
    case PD_SINK_REQUESTED:
    case PD_SINK_TRANSITION:
        if (sink_deadline(now_ms))
            sink_set_state(PD_SINK_SOFT_RESET, now_ms);
        break;
    case PD_SINK_READY:
        if (g_sink.epr_exit_pending != 0U)
        {
            union usbpd_epr_mode_do_u mode;
            mode.raw = 0U;
            mode.bits.action = USBPD_EPR_MODE_EXIT;
            if (sink_send_data(USBPD_DATA_EPR_MODE, &mode.raw, 1U, now_ms) == USBPD_OK)
            {
                g_sink.epr_exit_pending = 0U;
                g_sink.status.epr_active = 0U;
                sink_dpm_event(USBPD_DPM_EPR_EXITED, 0U, 0U);
                g_sink.epr_caps_valid = 0U;
                g_sink.target_dirty = 0U;
            }
        }
        else if (g_sink.epr_keepalive_pending != 0U)
        {
            if (sink_deadline(now_ms))
                sink_set_state(PD_SINK_HARD_RESET, now_ms);
        }
        else if (g_sink.target_dirty != 0U)
            sink_begin_selection(now_ms);
        else if ((g_sink.status.epr_active != 0U) && ((int32_t)(now_ms - g_sink.epr_keepalive_ms) >= 0))
        {
            if (usbpd_protocol_send_extended_control(USBPD_EXT_CTRL_EPR_KEEPALIVE, 0U, now_ms) == USBPD_OK)
            {
                g_sink.epr_keepalive_pending = 1U;
                g_sink.deadline_ms = now_ms + PD_SINK_SENDER_RESPONSE_MS;
            }
        }
        else if ((g_sink.select_kind == PD_SELECT_PPS) && ((int32_t)(now_ms - g_sink.pps_request_ms) >= 0))
        {
            (void)sink_send_request(now_ms);
        }
        break;
    case PD_SINK_SOFT_RESET:
        if ((g_sink.peer_soft_reset != 0U) &&
            (usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_ACCEPT, now_ms) == USBPD_OK))
        {
            g_sink.peer_soft_reset = 0U;
            sink_set_state((g_sink.status.contract_valid != 0U) ? PD_SINK_READY : PD_SINK_DISCOVERY,
                           (g_sink.status.contract_valid != 0U) ? 0U : now_ms + PD_SINK_WAIT_CAP_MS);
        }
        else if ((g_sink.peer_soft_reset == 0U) && (g_sink.soft_reset_sent == 0U) &&
                 (usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_SOFT_RESET, now_ms) == USBPD_OK))
        {
            g_sink.soft_reset_sent = 1U;
            g_sink.deadline_ms = now_ms + PD_SINK_SENDER_RESPONSE_MS;
        }
        else if ((g_sink.soft_reset_sent != 0U) && sink_deadline(now_ms))
        {
            sink_set_state(PD_SINK_HARD_RESET, now_ms);
        }
        break;
    case PD_SINK_HARD_RESET:
        if (g_sink.hard_reset_count >= USBPD_N_HARD_RESET_COUNT)
        {
            sink_set_state(PD_SINK_ERROR_RECOVERY, now_ms + PD_SINK_ERROR_RECOVERY_MS);
        }
        else if (usbpd_protocol_send_hard_reset(now_ms) == USBPD_OK)
        {
            g_sink.hard_reset_count++;
            if (g_sink.status.contract_valid != 0U)
                sink_dpm_event(USBPD_DPM_CONTRACT_LOST, 0U, 0U);
            sink_dpm_event(USBPD_DPM_HARD_RESET, 0U, 0U);
            sink_reset_contract();
            sink_set_state(PD_SINK_ERROR_RECOVERY, now_ms + PD_SINK_ERROR_RECOVERY_MS);
        }
        break;
    case PD_SINK_ERROR_RECOVERY:
        if (sink_deadline(now_ms))
        {
            usbpd_protocol_reset();
            g_sink.source_valid = 0U;
            g_sink.epr_caps_valid = 0U;
            g_sink.get_source_cap_sent = 0U;
            sink_set_state(PD_SINK_UNATTACHED, 0U);
        }
        break;
    default:
        sink_set_state(PD_SINK_UNATTACHED, 0U);
        break;
    }
    /* Printing is deliberately owned by usbpd_observer_service() in application context. */
}
