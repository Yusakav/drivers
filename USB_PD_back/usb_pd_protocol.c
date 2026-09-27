/**
 * @file usb_pd_protocol.c
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-12-14
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "usb_pd_protocol.h"
#include "usb_pd_phy.h"
#include <string.h>
#include <stdio.h>
#include "debug.h"
#include "timer.h"
#include "usb_vbus_measure.h"

#define PDDEBUG

#ifdef PDDEBUG
#define PDPRINT(format, ...) PRINT (format, ##__VA_ARGS__)
#else
#define PDPRINT(X...)
#endif

typedef void (*usbpd_notify_handler) (uint8_t event, void *arg);

/**
 * @brief USB PD 端口抽象
 *
 * 管理端口状态、角色、协议版本等。
 */
static struct usb_pd_port {
    enum usbpd_power_role_type_e power_role; /**< 当前电源角色配置 - @see usbpd_power_role_type_e */
    enum usbpd_data_role_type_e data_role;   /**< 当前数据角色配置 - @see usbpd_data_role_type_e */
    enum usbpd_typec_port_type_t port_type;  /**< 端口硬件类型 - @see port_type */
    enum usbpd_revision_t pd_revision;       /**< 协商的PD版本 */

    enum usbpd_cc_state_e cc_state;          /**< typec 附着状态 */

    enum usbpd_states_e task_state;          /**< 当前状态 */
    enum usbpd_states_e last_state;          /**< 上次状态 */
    enum usbpd_states_e timeout_state;       /**< 错误状态 */

    struct usbpd_phy_buffer_t rx_buf;        /**< pd消息缓冲区 */
    struct usbpd_phy_buffer_t tx_buf;        /**< pd消息缓冲区 */

    usbpd_notify_handler *notify_handler;

    union usbpd_pdo_u source_caps[7]; /**< source能力列表 */
    uint8_t source_cap_count;         /**< source能力数量（1-7）*/
    struct usbpd_pdo_manager_config_t pdo_config;
    uint32_t curr_limit;
    uint32_t supply_voltage;
    uint32_t supply_power;


    uint8_t cc_polarity; /**< typec 极性 */
    uint8_t message_id;  /**< 消息ID */
    uint8_t pdo_idx;     /**< 选择的sourcePDO索引 */

    /* 消息重传机制 */
    uint8_t retry_count;               /**< 当前重试次数 */
    uint8_t max_retry;                 /**< 最大重试次数 */
    uint8_t get_source_cap_sent;       /**< SNK_DISCOVERY 已发送 GetSourceCap 标志 */
    enum usbpd_sop_type_e last_sent_sop;     /**< 上次发送的 SOP 类型 */
    uint8_t last_sent_msg_type;              /**< 上次发送的消息类型 */
    uint8_t last_sent_payload[28];           /**< 上次发送的 payload 缓存 */
    uint8_t last_sent_payload_len;           /**< 上次发送的 payload 长度 */

    uint16_t vbus;       /**< mv */
    uint64_t timeout;
} pd[CONFIG_USB_PD_PORT_COUNT];

static const char *const pd_machine_state_names[] = {
    "DISABLED",
    "SUSPENDED",
#ifdef CONFIG_USB_PD_DUAL_ROLE
    "SNK_DISCONNECTED",
    "SNK_DISCONNECTED_DEBOUNCE",
    "SNK_ACCESSORY",
    "SNK_HARD_RESET_RECOVER",
    "SNK_DISCOVERY",
    "SNK_REQUESTED",
    "SNK_TRANSITION",
    "SNK_READY",
    "SNK_SWAP_INIT",
    "SNK_SWAP_SNK_DISABLE",
    "SNK_SWAP_SRC_DISABLE",
    "SNK_SWAP_STANDBY",
    "SNK_SWAP_COMPLETE",
#endif /* CONFIG_USB_PD_DUAL_ROLE */
    "SRC_DISCONNECTED",
    "SRC_DISCONNECTED_DEBOUNCE",
    "SRC_ACCESSORY",
    "SRC_HARD_RESET_RECOVER",
    "SRC_STARTUP",
    "SRC_DISCOVERY",
    "SRC_NEGOTIATE",
    "SRC_ACCEPTED",
    "SRC_POWERED",
    "SRC_TRANSITION",
    "SRC_READY",
    "SRC_GET_SNK_CAP",
    "DR_SWAP",
#ifdef CONFIG_USB_PD_DUAL_ROLE
    "SRC_SWAP_INIT",
    "SRC_SWAP_SNK_DISABLE",
    "SRC_SWAP_SRC_DISABLE",
    "SRC_SWAP_STANDBY",
#ifdef CONFIG_USBC_VCONN_SWAP
    "VCONN_SWAP_SEND",
    "VCONN_SWAP_INIT",
    "VCONN_SWAP_READY",
#endif /* CONFIG_USBC_VCONN_SWAP */
#endif /* CONFIG_USB_PD_DUAL_ROLE */
    "SOFT_RESET",
    "HARD_RESET_SEND",
    "HARD_RESET_EXECUTE",
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
    "DRP_AUTO_TOGGLE",
#endif
};

static const char *const pd_cc_volt_state_names[] = {
    "Open",
    "Ra (Cable)",
    "Rd (Sink Device)",
    "Rp-Default",
    "Rp-1.5A",
    "Rp-3.0A",
};

static void set_state_machine_timeout (int port, uint64_t timeout, enum usbpd_states_e timeout_state) {
    pd[port].timeout = timeout;
    pd[port].timeout_state = timeout_state;
}

static void set_state_machine_state (int port, enum usbpd_states_e next_state) {
    enum usbpd_states_e last_state = pd[port].task_state;

    set_state_machine_timeout (port, 0, 0);

    pd[port].task_state = next_state;

    PDPRINT ("%05d, [PD]: %05dmv, %s - > %s\n", (int)get_time().val, (int)adc_get_vbus_mv(), pd_machine_state_names[last_state], pd_machine_state_names[next_state]);

    if (last_state == next_state)
        return;
}

static inline int cc_is_rp (enum usbpd_cc_voltage_status_e cc) {
    return (cc == TYPEC_CC_VOLT_RP_DEF) || (cc == TYPEC_CC_VOLT_RP_1_5) ||
           (cc == TYPEC_CC_VOLT_RP_3_0);
}

static int pd_set_data_role (int port, enum usbpd_data_role_type_e role) {
    pd[port].data_role = role;
    return USBPD_OK;
}

/**
 * @brief 初始化 USB PD 端口
 * @param port 指向端口结构体
 */
void usb_pd_init (int port) {
    int res;
    memset (&pd[port], 0, sizeof (struct usb_pd_port));

    res = usb_pd_phy_init();
    if (res != USBPD_OK) {
        pd[port].task_state = PD_STATE_SUSPENDED;
    } else {
#if CONFIG_USB_PD_ROLE_DEFAULT
        pd[port].task_state = PD_STATE_SRC_DISCONNECTED;
#else
        pd[port].task_state = PD_STATE_SNK_DISCONNECTED;
#endif
    }

#if CONFIG_USB_PD_ROLE_DEFAULT
    pd[port].port_type = USBPD_TYPEC_PORT_SRC;
#else
    pd[port].port_type = USBPD_TYPEC_PORT_SNK;
#endif

#if CONFIG_USB_PD_ROLE_DEFAULT
    usb_pd_phy_set_pull (TYPEC_CC_PULL_RP);
    /* 配置默认 Source Capabilities (5V/3A Fixed PDO) */
    pd[port].source_caps[0].d32 = 0;
    pd[port].source_caps[0].fixed.max_current = 300;  /* 300 * 10mA = 3A */
    pd[port].source_caps[0].fixed.voltage = 100;       /* 100 * 50mV = 5V */
    pd[port].source_caps[0].fixed.pdo_type = USBPD_PDO_TYPE_FIXED;
    pd[port].source_cap_count = 1;
    pd[port].power_role = USBPD_POWER_ROLE_SOURCE;
#else
    // usb_pd_phy_set_pull(TYPEC_CC_PULL_RD);
#endif

    pd[port].timeout = 10 * MSEC;

    pd[port].pdo_config.strategy = UBSPD_PDO_STRATEGY_MAX_VOLTAGE;
    pd[port].pdo_config.target_mv = 20000;
    pd[port].pdo_config.min_mv = 5000;
    pd[port].pdo_config.max_mv = 20000;
    pd[port].pdo_config.min_ma = 1000;


    PRINT ("[PD] Initialized\n");
}

static int pd_extract_pdo_power (const union usbpd_pdo_u *src_cap, uint32_t *ma, uint32_t *mv, uint32_t *mw) {
    if (!src_cap || !ma || !mv || !mw) {
        return USBPD_ERR;
    }
    *ma = 0;
    *mv = 0;
    *mw = 0;
    switch (src_cap->general.pdo_type) {
    case USBPD_PDO_TYPE_FIXED:
        *ma = src_cap->fixed.max_current * 10;
        *mv = src_cap->fixed.voltage * 50;
        *mw = (*mv * *ma) / 1000;
        break;
    case USBPD_PDO_TYPE_BATTERY:
        break;
    case USBPD_PDO_TYPE_VARIABLE:
        break;
    case USBPD_PDO_TYPE_APDO:
        break;
    default:
        break;
    }
    return USBPD_OK;
}

static int pdo_find_index (const union usbpd_pdo_u *src_caps, uint8_t pdo_cnt, const struct usbpd_pdo_manager_config_t *cfg) {
    if (!src_caps || !pdo_cnt) {
        return USBPD_ERR;
    }

    uint32_t i, best_score = 0;
    int best_index = -1;

    for (i = 0; i < pdo_cnt; i++, src_caps++) {
        uint32_t mw = 0;
        uint32_t mv = 0;
        uint32_t ma = 0;
        pd_extract_pdo_power (src_caps, &ma, &mv, &mw);

        if (mv > cfg->max_mv)
            continue;
        if (ma < cfg->min_ma)
            continue;

        switch (cfg->strategy) {
        case UBSPD_PDO_STRATEGY_MAX_POWER:
            if (mw >= best_score) {
                best_score = mw;
                best_index = i;
            }
            break;

        case UBSPD_PDO_STRATEGY_MAX_VOLTAGE:
            if (mv >= best_score) {
                best_score = mv;
                best_index = i;
            }
            break;

        case UBSPD_PDO_STRATEGY_MIN_VOLTAGE:
            if ((best_index == -1) ||
                (mv < best_score)) {
                best_score = mv;
                best_index = i;
            }
            break;

        case UBSPD_PDO_STRATEGY_MATCH_VOLTAGE:
            if (mv == cfg->target_mv) {
                return i;
            }
            break;

        case UBSPD_PDO_STRATEGY_VOLTAGE_WINDOW:
            if ((mv >= cfg->min_mv) &&
                (mv <= cfg->max_mv)) {
                if (mw >= best_score) {
                    best_score = mw;
                    best_index = i;
                }
            }
            break;

        case UBSPD_PDO_STRATEGY_PREFER_PPS:
            if ((src_caps->general.pdo_type == USBPD_PDO_TYPE_APDO) &&
                (mv >= cfg->min_mv) &&
                (mv <= cfg->max_mv)) {
                return i;
            }
            if (mw >= best_score) {
                best_score = mw;
                best_index = i;
            }
            break;
        }
    }

    return best_index;
}

/**
 * @brief 获取底层 PD 协议状态机当前状态
 * @param port 端口号
 * @return 当前状态枚举值
 */
enum usbpd_states_e usb_pd_get_state (int port)
{
    return pd[port].task_state;
}

/**
 * @brief 获取 PD 协商结果
 * @param port       端口号
 * @param voltage_mv 输出：协商电压 (mV)
 * @param current_ma 输出：协商电流 (mA)
 * @param power_mw   输出：协商功率 (mW)
 * @param pdo_idx    输出：选中的 PDO 索引
 * @return USBPD_OK 成功，USBPD_ERR 失败
 */
int usb_pd_get_negotiation (int port, uint32_t *voltage_mv, uint32_t *current_ma, uint32_t *power_mw, uint8_t *pdo_idx)
{
    if (!voltage_mv || !current_ma || !power_mw || !pdo_idx) {
        return USBPD_ERR;
    }
    *voltage_mv = pd[port].supply_voltage;
    *current_ma = pd[port].curr_limit;
    *power_mw   = pd[port].supply_power;
    *pdo_idx    = pd[port].pdo_idx;
    return USBPD_OK;
}

/**
 * @brief 注册 Source PDO 列表
 * @param port      端口号
 * @param pdos      PDO 数组指针
 * @param pdo_count PDO 数量（1-7）
 * @return USBPD_OK 成功，USBPD_ERR 失败
 */
int usb_pd_set_source_caps(int port, const union usbpd_pdo_u *pdos, uint8_t pdo_count)
{
    if (!pdos || pdo_count == 0 || pdo_count > 7) {
        return USBPD_ERR;
    }

    memcpy(pd[port].source_caps, pdos, pdo_count * sizeof(union usbpd_pdo_u));
    pd[port].source_cap_count = pdo_count;
    return USBPD_OK;
}

static int pd_send_message (int port, enum usbpd_sop_type_e sop, uint8_t msg_type, uint8_t *payload, uint16_t payload_len) {

    int res;

    pd[port].tx_buf.header.msg_header.msg_type = msg_type;
    pd[port].tx_buf.header.msg_header.data_role = pd[port].data_role;
    pd[port].tx_buf.header.msg_header.specs_rev = pd[port].pd_revision;
    pd[port].tx_buf.header.msg_header.power_role = pd[port].power_role;
    pd[port].tx_buf.header.msg_header.msg_id = pd[port].message_id;
    pd[port].tx_buf.header.msg_header.n_data_obj = payload_len;
    pd[port].tx_buf.header.msg_header.extended = 0;


    pd[port].tx_buf.sop = sop;
    pd[port].tx_buf.payload_len = payload_len;
    if (payload_len > 0 && payload) {
        memcpy(&pd[port].tx_buf.payload, payload, payload_len*4);
    }
    pd[port].tx_buf.flag.is_ready = 1;

    res = usb_pd_phy_send_msg (&pd[port].tx_buf);
    if (res == USBPD_OK) {
        pd[port].message_id = (pd[port].message_id + 1) & 0x07;

        /* 保存消息用于重传 */
        pd[port].retry_count = 0;
        pd[port].max_retry = 3;
        pd[port].last_sent_sop = sop;
        pd[port].last_sent_msg_type = msg_type;
        pd[port].last_sent_payload_len = payload_len;
        if (payload_len > 0 && payload) {
            memcpy(pd[port].last_sent_payload, payload, payload_len * 4);
        }
    }
    return res;
}

static int pd_send_request (int port, int index, uint16_t type, uint32_t ma, uint32_t mv, uint32_t mw) {

    union usbpd_rdo_u rdo = {0};
    uint16_t payload_len = 0;

    switch (type) {
    case USBPD_PDO_TYPE_FIXED:
        rdo.fixed_variable.current_extremum_10ma = ma / 10;
        rdo.fixed_variable.current_operate_10ma = ma / 10;
        rdo.fixed_variable.object_position = index + 1;
        rdo.fixed_variable.no_usb_suspend = 1;
        rdo.fixed_variable.usb_comm_capable = 0;
        payload_len = 1;
        break;
    case USBPD_PDO_TYPE_BATTERY:
        break;
    case USBPD_PDO_TYPE_VARIABLE:
        break;
    case USBPD_PDO_TYPE_APDO:
        break;
    default:
        break;
    }

    return pd_send_message (port, USBPD_SOP_TYPE_SOP, USBPD_DATA_MSG_REQUEST, (uint8_t *)&rdo.d32, payload_len);
}

static int handle_ctrl_msg (int port, const union usbpd_message_header_u *header, const uint8_t *payload, uint16_t payload_len) {
    if (!header || (payload_len > 0 && !payload)) {
        return USBPD_ERR;
    }
    switch (header->msg_header.msg_type) {
    //  GoodCRC   0b00001：GoodCRC —— 用来确认对方发送的消息, 限定时间内
    case USBPD_CTRL_MSG_GOOD_CRC:
        {
            uint8_t rx_msg_id = header->msg_header.msg_id;
            uint8_t expected_msg_id = (pd[port].message_id > 0) ? (pd[port].message_id - 1) : 7;
            if (rx_msg_id == expected_msg_id) {
                /* GoodCRC 匹配上次发送的消息 ID，可清除等待响应标志 */
            }
        }
        break;
    //  GotoMin   0b00010：GotoMin —— 请求降至最小电压，适用于
    case USBPD_CTRL_MSG_GO_TO_MIN:
        break;
    //  Accept    0b00011：Accept —— 同意前一条消息, 限定时间内
    case USBPD_CTRL_MSG_ACCEPT:
        if (pd[port].task_state == PD_STATE_SNK_REQUESTED) {
            set_state_machine_state (port, PD_STATE_SNK_TRANSITION);
        }
        break;
    //  Reject    0b00100：Reject —— 拒绝前一条消息, 限定时间内
    case USBPD_CTRL_MSG_REJECT:
        break;
    //  Ping      0b00101：Ping —— 链路心跳
    case USBPD_CTRL_MSG_PING:
        break;
    //  PSRDY     0b00110：PS_RDY —— 电源准备完成
    case USBPD_CTRL_MSG_PS_READY:
        if (pd[port].task_state == PD_STATE_SNK_TRANSITION) {
            set_state_machine_state (port, PD_STATE_SNK_READY);
        }
        break;
    //  GetSourceCap  0b00111：请求 Source Capabilities
    case USBPD_CTRL_MSG_GET_SOURCE_CAP:
        break;
    //  GetSinkCap    0b01000：请求 Sink Capabilities
    case USBPD_CTRL_MSG_GET_SINK_CAP:
        break;
    //  DRSwap         0b01001：数据角色交换（DFP/UFP）
    case USBPD_CTRL_MSG_DR_SWAP:
        break;
    //  PRSwap         0b01010：电源角色交换（SRC/SNK）
    case USBPD_CTRL_MSG_PR_SWAP:
        break;
    //  VconnSwap      0b01011：VCONN 交换
    case USBPD_CTRL_MSG_VCONN_SWAP:
        break;
    //  Wait               0b01100：Wait —— 需要更多时间处理
    case USBPD_CTRL_MSG_WAIT:
        break;
    //  SoftReset          0b01101：Soft Reset（软件重置）
    case USBPD_CTRL_MSG_SOFT_RESET:
        break;
    //  DataReset     0b01110：数据路径重置
    case USBPD_CTRL_MSG_DATA_RESET:
        break;
    //  DataResetComplete     0b01111：数据路径重置完成
    case USBPD_CTRL_MSG_DATA_RESET_COMPLETE:
        break;
    //  NotSupported  0b10000：不支持消息
    case USBPD_CTRL_MSG_NOT_SUPPORTED:
        break;
    // GetSourceCapExt   0b10001：请求扩展源能力
    case USBPD_CTRL_MSG_GET_SOURCE_CAP_EXTENDED:
        break;
    //  GetStatus     0b10010：请求状态信息
    case USBPD_CTRL_MSG_GET_STATUS:
        break;
    //  FRSwap    0b10011：快速角色切换（Fast Role Swap）
    case USBPD_CTRL_MSG_FR_SWAP:
        break;
    //  GetPPSStatus  0b10100：请求 PPS 状态
    case USBPD_CTRL_MSG_GET_PPS_STATUS:
        break;
    //  GetCountryCodes   0b10101：请求国家代码
    case USBPD_CTRL_MSG_GET_COUNTRY_CODES:
        break;
    //  GetSinkCapExt     0b10110：请求扩展 Sink 能力
    case USBPD_CTRL_MSG_GET_SINK_CAP_EXTENDED:
        break;
    //  GetSourceInfo     0b10111：请求 Source 信息
    case USBPD_CTRL_MSG_GET_SOURCE_INFO:
        break;
    case USBPD_CTRL_MSG_REVISION:  //  GetRevision   0b11000：请求 USB PD 协议版本
        break;
    }
    return USBPD_OK;
}

static int handle_data_msg (int port, const union usbpd_message_header_u *header, const uint8_t *payload, uint16_t payload_len) {
    if (!header || !payload || !payload_len || payload_len > 7) {
        return USBPD_ERR;
    }

    int res;

    switch (header->msg_header.msg_type) {
    // SourceCap, 0b00001：Source Capabilities（供电能力列表，包含多个 PDO）
    case USBPD_DATA_MSG_SOURCE_CAP:
        if ((pd[port].task_state != PD_STATE_SNK_DISCOVERY) &&
            (pd[port].task_state != PD_STATE_SNK_TRANSITION)) {
            return USBPD_ERR;
        }

        for (pd[port].source_cap_count = 0;
             pd[port].source_cap_count < payload_len;
             pd[port].source_cap_count++) {
            memcpy (&pd[port].source_caps[pd[port].source_cap_count].d32,
                    payload + pd[port].source_cap_count * 4,
                    4);
        }

        res = pdo_find_index (pd[port].source_caps, pd[port].source_cap_count, &pd[port].pdo_config);
        if (res == -1) {
            return USBPD_ERR;
        }
        pd[port].pdo_idx = res;
        res = pd_extract_pdo_power (&pd[port].source_caps[pd[port].pdo_idx], &pd[port].curr_limit, &pd[port].supply_voltage, &pd[port].supply_power);

        PDPRINT ("%05d [PD] Request: %dmV, %dmA, %dmW\n", (int)get_time().val, pd[port].supply_voltage, pd[port].curr_limit, pd[port].supply_power);

        res = pd_send_request (port, pd[port].pdo_idx, pd[port].source_caps[pd[port].pdo_idx].general.pdo_type, pd[port].curr_limit, pd[port].supply_voltage, pd[port].supply_power);

        if (res == USBPD_OK) {
            set_state_machine_state (port, PD_STATE_SNK_REQUESTED);
        } else {
            set_state_machine_state (port, PD_STATE_SUSPENDED);
        }
        return res;
        break;
    // Request, 0b00010：Request —— 请求某个 PDO
    case USBPD_DATA_MSG_REQUEST:
        {
            union usbpd_rdo_u rdo;
            uint8_t obj_pos;
            uint16_t req_current_ma;

            if (payload_len < 1) {
                break;
            }

            memcpy(&rdo.d32, payload, sizeof(rdo.d32));
            obj_pos = rdo.fixed_variable.object_position;

            /* 验证 PDO 索引是否在有效范围内 */
            if (obj_pos < 1 || obj_pos > pd[port].source_cap_count) {
                pd_send_message(port, USBPD_SOP_TYPE_SOP, USBPD_CTRL_MSG_REJECT, NULL, 0);
                set_state_machine_state(port, PD_STATE_SRC_ACCEPTED);
                break;
            }

            {
                union usbpd_pdo_u *pdo = &pd[port].source_caps[obj_pos - 1];

                /* 对 Fixed PDO 验证请求电流是否超限 */
                if (pdo->general.pdo_type == USBPD_PDO_TYPE_FIXED) {
                    req_current_ma = rdo.fixed_variable.current_operate_10ma * 10;
                    uint16_t max_current_ma = pdo->fixed.max_current * 10;

                    if (req_current_ma > max_current_ma) {
                        pd_send_message(port, USBPD_SOP_TYPE_SOP, USBPD_CTRL_MSG_REJECT, NULL, 0);
                    } else {
                        pd_send_message(port, USBPD_SOP_TYPE_SOP, USBPD_CTRL_MSG_ACCEPT, NULL, 0);
                    }
                } else {
                    /* 非 Fixed PDO 直接 Accept */
                    pd_send_message(port, USBPD_SOP_TYPE_SOP, USBPD_CTRL_MSG_ACCEPT, NULL, 0);
                }
            }

            set_state_machine_state(port, PD_STATE_SRC_ACCEPTED);
        }
        break;
    // BIST, 0b00011：BIST 测试模式
    case USBPD_DATA_MSG_BIST:
        break;
    // SinkCap, 0b00100：Sink Capabilities（受电端能力）
    case USBPD_DATA_MSG_SINK_CAP:
        break;
    // BatteryStatus, 0b00101：电池状态
    case USBPD_DATA_MSG_BATTERY_STATUS:
        break;
    // Alert, 0b00110：告警信息
    case USBPD_DATA_MSG_ALERT:
        break;
    // GetCountryInfo, 0b00111：获取国家/地区信息
    case USBPD_DATA_MSG_GET_COUNTRY_INFO:
        break;
    // EnterUSB, 0b01000：进入 USB 模式（USB4/Alt Mode）
    case USBPD_DATA_MSG_ENTER_USB:
        break;
    // EPRRequest, 0b01001：EPR 扩展功率范围请求
    case USBPD_DATA_MSG_EPR_REQUEST:
        break;
    // EPRMode, 0b01010：EPR 模式
    case USBPD_DATA_MSG_EPR_MODE:
        break;
    // SourceInfo, 0b01011：源端信息
    case USBPD_DATA_MSG_SRC_INFO:
        break;
    // Revision, 0b01100：PD 规范版本信息
    case USBPD_DATA_MSG_REVISION:
        break;
    // VendorDefined, 0b01111：VDM —— 厂商自定义数据
    case USBPD_DATA_MSG_VENDOR_DEFINED:
        break;
    }
    return USBPD_OK;
}

static int handle_ext_msg (int port, const union usbpd_message_header_u *header, const uint8_t *payload, uint16_t payload_len) {
    if (!header || !payload || !payload_len) {
        return USBPD_ERR;
    }

    switch (header->msg_header.msg_type) {
    // SourceCapExt       0b00001：扩展 Source Capabilities
    case USBPD_EXTENDED_MSG_SOURCE_CAP_EXTENDED:
        break;
    // Status             0b00010：设备状态（Status）
    case USBPD_EXTENDED_MSG_STATUS:
        break;
    // GetBatteryCap      0b00011：请求电池容量信息
    case USBPD_EXTENDED_MSG_GET_BATTERY_CAP:
        break;
    // GetBatteryStatus   0b00100：请求电池状态
    case USBPD_EXTENDED_MSG_GET_BATTERY_STATUS:
        break;
    // BatteryCap         0b00101：电池容量信息（Battery Capabilities）
    case USBPD_EXTENDED_MSG_BATTERY_CAP:
        break;
    // GetMfrInfo         0b00110：请求制造商信息
    case USBPD_EXTENDED_MSG_GET_MANUFACTURER_INFO:
        break;
    // MfrInfo            0b00111：制造商信息响应
    case USBPD_EXTENDED_MSG_MANUFACTURER_INFO:
        break;
    // SecurityReq        0b01000：安全请求（Authentication）
    case USBPD_EXTENDED_MSG_SECURITY_REQUEST:
        break;
    // SecurityResp       0b01001：安全响应
    case USBPD_EXTENDED_MSG_SECURITY_RESPONSE:
        break;
    // FWUpdateReq        0b01010：固件更新请求（FW Update）
    case USBPD_EXTENDED_MSG_FIRMWARE_UPDATE_REQUEST:
        break;
    // FWUpdateResp       0b01011：固件更新响应
    case USBPD_EXTENDED_MSG_FIRMWARE_UPDATE_RESPONSE:
        break;
    // PPSStatus          0b01100：PPS 状态（增强 PPS 反馈）
    case USBPD_EXTENDED_MSG_PPS_STATUS:
        break;
    // CountryInfo        0b01101：国家/地区信息
    case USBPD_EXTENDED_MSG_COUNTRY_INFO:
        break;
    // CountryCodes       0b01110：国家代码列表
    case USBPD_EXTENDED_MSG_COUNTRY_CODES:
        break;
    // SinkCapExt         0b01111：扩展 Sink Capabilities
    case USBPD_EXTENDED_MSG_SINK_CAP_EXTENDED:
        break;
    // ExtControl         0b10000：Extended Control message（工程模式/厂商测试）
    case USBPD_EXTENDED_MSG_EXTENDED_CONTROL:
        break;
    // EPRSourceCap       0b10001：EPR 专用 Source Cap（超高功率）
    case USBPD_EXTENDED_MSG_EPR_SOURCE_CAP:
        break;
    // EPRSinkCap         0b10010：EPR 专用 Sink Cap
    case USBPD_EXTENDED_MSG_EPR_SINK_CAP:
        break;
    // VendorDefinedExt   0b11111：VDM —— 厂商自定义数据
    case USBPD_EXTENDED_MSG_VENDOR_DEFINED:
        break;
    }
    return USBPD_OK;
}

/**
 * @brief 处理接收到的 USB PD 消息，并执行相应操作
 *
 * @param type 消息类型类型
 * @param buf 接收的消息缓冲区
 * @param len 消息长度（字节）
 *
 * @note 当 "Extended" 字段为 0 时：
 *  - "Data Object Count"（3位）字段表示消息头之后所跟随的 32-bit 数据对象（Data Object）的数量。
 *  - 当 Data Object Count = 0 时，该消息为 **控制消息（Control Message）**；
 *  - 当 Data Object Count ≠ 0 时，该消息为 **数据消息（Data Message）**。
 */
int handle_msg (int port, const struct usbpd_phy_buffer_t *pkt) {
    if (!pkt ) {
        return USBPD_ERR;
    }

    /* 检测 Hard Reset SOP */
    if (pkt->sop == USBPD_SOP_TYPE_HARD_RESET) {
        set_state_machine_state(port, PD_STATE_HARD_RESET_EXECUTE);
        return USBPD_OK;
    }

    pd[port].pd_revision = pkt->header.msg_header.specs_rev;

    if (pkt->header.msg_header.extended) {
        /* 扩展消息处理 */
        return handle_ext_msg (port, &pkt->header, pkt->payload, pkt->payload_len);
    } else if (pkt->header.msg_header.n_data_obj == 0) {
        /* 控制消息处理 */
        return handle_ctrl_msg (port, &pkt->header, pkt->payload, pkt->payload_len);
    } else {
        /* 数据消息处理 */
        return handle_data_msg (port, &pkt->header, pkt->payload, pkt->payload_len);
        
    }

    return USBPD_ERR;
}

/**
 * @brief USB PD 端口循环处理函数
 * @param port 端口号
 */
void usb_pd_process (int port) {
    static union timestamp_u now;
    int timeout = 10000;

    static enum usbpd_cc_state_e cc_next_state;
    enum usbpd_cc_voltage_status_e cc1_volt;
    enum usbpd_cc_voltage_status_e cc2_volt;
    enum usbpd_states_e this_state;

    if (usb_pd_phy_get_msg (&pd[port].rx_buf) == USBPD_OK) {
        handle_msg (port, &pd[port].rx_buf);
    }

    this_state = pd[port].task_state;
    switch (this_state) {
    case PD_STATE_DISABLED:
        /* PD功能禁用状态 */
        break;

    case PD_STATE_SUSPENDED:
        /* PD挂起/暂停状态 */
        break;

#ifdef CONFIG_USB_PD_DUAL_ROLE
    /* --- 设备模式 (Sink - 受电端) 状态 --- */
    case PD_STATE_SNK_DISCONNECTED:
        /* 受电端断开连接状态 */

        usb_pd_phy_get_cc_state (port, &cc1_volt, &cc2_volt);

        if (cc1_volt != TYPEC_CC_VOLT_OPEN || cc2_volt != TYPEC_CC_VOLT_OPEN) {
            pd[port].cc_state = USBPD_CC_STATE_UNATTACHED;
            set_state_machine_state (port, PD_STATE_SNK_DISCONNECTED_DEBOUNCE);
            set_state_machine_timeout (port, get_time().val + USBPD_T_CC_DEBOUNCE, PD_STATE_SUSPENDED);
        }
        break;

    case PD_STATE_SNK_DISCONNECTED_DEBOUNCE:
        /* 受电端断开连接去抖状态 */

        usb_pd_phy_get_cc_state (port, &cc1_volt, &cc2_volt);

        if (cc_is_rp (cc1_volt) && cc_is_rp (cc2_volt)) {
            cc_next_state = USBPD_CC_STATE_DEBUG_ACC;
        } else if (cc_is_rp (cc1_volt) || cc_is_rp (cc2_volt)) {
            cc_next_state = USBPD_CC_STATE_ATTACHED_DFP;
        } else {
            set_state_machine_state (port, PD_STATE_SNK_DISCONNECTED);
            timeout = 5 * MSEC;
            break;
        }

        if (cc_next_state != pd[port].cc_state) {
            pd[port].cc_state = cc_next_state;
            set_state_machine_timeout (port, get_time().val + USBPD_T_CC_DEBOUNCE, PD_STATE_SNK_DISCONNECTED);
            break;
        }

        if (usb_phy_get_vbus ((int *)&pd[port].vbus) != USBPD_OK) {
            break;
        }

        pd[port].cc_polarity = (cc1_volt > cc2_volt) ? 1 : 2;

        usb_pd_phy_set_sel (pd[port].cc_polarity);
        pd_set_data_role (port, USBPD_DATA_ROLE_UFP);
        set_state_machine_state (port, PD_STATE_SNK_DISCOVERY);

        break;

    case PD_STATE_SNK_ACCESSORY:
        /* 受电端附件模式状态 */
        break;

    case PD_STATE_SNK_HARD_RESET_RECOVER:
        /* 受电端硬复位恢复状态 */
        break;

    case PD_STATE_SNK_DISCOVERY:
        /* 受电端设备发现状态 */
        if (pd[port].last_state != pd[port].task_state) {
            pd[port].get_source_cap_sent = 0;
            set_state_machine_timeout (port, get_time().val + USBPD_T_SINK_WAIT_CAP, PD_STATE_SOFT_RESET);
        }
        break;

    case PD_STATE_SNK_REQUESTED:
        /* 受电端电源请求状态 */
        if (pd[port].last_state != pd[port].task_state) {

            set_state_machine_timeout (port, get_time().val + USBPD_T_SENDER_RESPONSE, PD_STATE_SOFT_RESET);
        }
        break;

    case PD_STATE_SNK_TRANSITION:
        /* 受电端电源转换状态 */
        if (pd[port].last_state != pd[port].task_state) {

            set_state_machine_timeout (port, get_time().val + USBPD_T_PS_TRANSITION, PD_STATE_SOFT_RESET);
        }
        break;

    case PD_STATE_SNK_READY:
        /* 受电端就绪状态 */
        if (pd[port].last_state != pd[port].task_state) {

           PDPRINT ("%05d [PD] Request: %dmV\n", (int)get_time().val, (int)adc_get_vbus_mv());

        }
        break;

    /* --- 受电端角色交换 (Sink Swap) 相关状态 --- */
    case PD_STATE_SNK_SWAP_INIT:
        /* 受电端交换初始化状态 */
        break;

    case PD_STATE_SNK_SWAP_SNK_DISABLE:
        /* 受电端交换中禁用当前受电功能状态 */
        break;

    case PD_STATE_SNK_SWAP_SRC_DISABLE:
        /* 受电端交换中禁用供电功能状态 */
        break;

    case PD_STATE_SNK_SWAP_STANDBY:
        /* 受电端交换待机状态 */
        break;

    case PD_STATE_SNK_SWAP_COMPLETE:
        /* 受电端交换完成状态 */
        break;
#endif /* CONFIG_USB_PD_DUAL_ROLE */

    /* --- 主机模式 (Source - 供电端) 状态 --- */
    case PD_STATE_SRC_DISCONNECTED:
        /* 供电端断开连接状态 — 检测 Sink 设备附着 (Rd) */
        usb_pd_phy_get_cc_state (port, &cc1_volt, &cc2_volt);

        if (cc1_volt == TYPEC_CC_VOLT_RD || cc2_volt == TYPEC_CC_VOLT_RD) {
            pd[port].cc_polarity = (cc1_volt == TYPEC_CC_VOLT_RD) ? 1 : 2;
            set_state_machine_state (port, PD_STATE_SRC_DISCONNECTED_DEBOUNCE);
            set_state_machine_timeout (port, get_time().val + USBPD_T_CC_DEBOUNCE,
                                       PD_STATE_SRC_DISCONNECTED);
        }
        break;

    case PD_STATE_SRC_DISCONNECTED_DEBOUNCE:
        /* 供电端断开连接去抖状态 — 确认 Rd 稳定 */
        usb_pd_phy_get_cc_state (port, &cc1_volt, &cc2_volt);

        if (cc1_volt == TYPEC_CC_VOLT_RD || cc2_volt == TYPEC_CC_VOLT_RD) {
            set_state_machine_state (port, PD_STATE_SRC_STARTUP);
        } else {
            set_state_machine_state (port, PD_STATE_SRC_DISCONNECTED);
        }
        break;

    case PD_STATE_SRC_ACCESSORY:
        /* 供电端附件模式状态 */
        break;

    case PD_STATE_SRC_HARD_RESET_RECOVER:
        /* 供电端硬复位恢复状态 */
        break;

    case PD_STATE_SRC_STARTUP:
        /* 供电端启动状态 — 使能 VBUS，设置初始 5V */
        if (pd[port].last_state != pd[port].task_state) {
            pd[port].vbus = 5000;
            pd[port].power_role = USBPD_POWER_ROLE_SOURCE;
            usb_pd_phy_set_sel (pd[port].cc_polarity);
            pd_set_data_role (port, USBPD_DATA_ROLE_DFP);
            set_state_machine_timeout (port, get_time().val + USBPD_T_SRC_TURN_ON,
                                       PD_STATE_SRC_DISCOVERY);
        }
        break;

    case PD_STATE_SRC_DISCOVERY:
        /* 供电端设备发现状态 — 发送 Source Capabilities */
        if (pd[port].last_state != pd[port].task_state) {
            pd_send_message (port, USBPD_SOP_TYPE_SOP, USBPD_DATA_MSG_SOURCE_CAP,
                             (uint8_t *)pd[port].source_caps,
                             pd[port].source_cap_count);
            set_state_machine_timeout (port, get_time().val + USBPD_T_SENDER_RESPONSE,
                                       PD_STATE_SOFT_RESET);
        }
        break;

    case PD_STATE_SRC_NEGOTIATE:
        /* 供电端协商状态 — 已发送 Accept，等待 Sink 转换 */
        if (pd[port].last_state != pd[port].task_state) {
            set_state_machine_timeout (port, get_time().val + USBPD_T_PS_TRANSITION,
                                       PD_STATE_SOFT_RESET);
        }
        break;

    case PD_STATE_SRC_ACCEPTED:
        /* 供电端接受状态 — 等待 PS_RDY (由 handle_ctrl_msg 驱动跳转) */
        if (pd[port].last_state != pd[port].task_state) {
            set_state_machine_timeout (port, get_time().val + USBPD_T_PS_TRANSITION,
                                       PD_STATE_SOFT_RESET);
        }
        break;

    case PD_STATE_SRC_POWERED:
        /* 供电端已供电状态 */
        break;

    case PD_STATE_SRC_TRANSITION:
        /* 供电端电源转换状态 */
        break;

    case PD_STATE_SRC_READY:
        /* 供电端就绪状态 — 持续监控 */
        if (pd[port].last_state != pd[port].task_state) {
            PDPRINT ("%05d [PD] SRC_READY: %dmV\n",
                     (int)get_time().val, (int)adc_get_vbus_mv());
        }
        break;

    case PD_STATE_SRC_GET_SINK_CAP:
        /* 供电端获取受电端能力状态 */
        break;

    case PD_STATE_DR_SWAP:
        /* 双角色交换状态 (角色互换) */
        break;

#ifdef CONFIG_USB_PD_DUAL_ROLE
    /* --- 供电端角色交换 (Source Swap) 相关状态 --- */
    case PD_STATE_SRC_SWAP_INIT:
        /* 供电端交换初始化状态 */
        break;

    case PD_STATE_SRC_SWAP_SNK_DISABLE:
        /* 供电端交换中禁用受电功能状态 */
        break;

    case PD_STATE_SRC_SWAP_SRC_DISABLE:
        /* 供电端交换中禁用供电功能状态 */
        break;

    case PD_STATE_SRC_SWAP_STANDBY:
        /* 供电端交换待机状态 */
        break;

#ifdef CONFIG_USBC_VCONN_SWAP
    /* --- VCONN (线缆供电) 交换相关状态 --- */
    case PD_STATE_VCONN_SWAP_SEND:
        /* 发送VCONN交换请求状态 */
        break;

    case PD_STATE_VCONN_SWAP_INIT:
        /* VCONN交换初始化状态 */
        break;

    case PD_STATE_VCONN_SWAP_READY:
        /* VCONN交换就绪状态 */
        break;
#endif /* CONFIG_USBC_VCONN_SWAP */
#endif /* CONFIG_USB_PD_DUAL_ROLE */

    /* --- 复位相关状态 --- */
    case PD_STATE_SOFT_RESET:
        /* 软复位状态：复位 message_id 并发送 SoftReset */
        if (pd[port].last_state != pd[port].task_state) {
            PDPRINT("%05d [PD] Soft Reset Send\n", (int)get_time().val);
            pd[port].message_id = 0;
            pd[port].retry_count = 0;
            pd_send_message(port, USBPD_SOP_TYPE_SOP, USBPD_CTRL_MSG_SOFT_RESET, NULL, 0);
            set_state_machine_timeout(port, get_time().val + USBPD_T_SENDER_RESPONSE, PD_STATE_SOFT_RESET);
            /* 根据当前角色跳转对应断开状态 */
            if (pd[port].power_role == USBPD_POWER_ROLE_SOURCE) {
                set_state_machine_state(port, PD_STATE_SRC_DISCONNECTED);
            } else {
                set_state_machine_state(port, PD_STATE_SNK_DISCONNECTED);
            }
        }
        break;

    case PD_STATE_HARD_RESET_SEND:
        /* 发送硬复位状态 */
        if (pd[port].last_state != pd[port].task_state) {
            PDPRINT("%05d [PD] Hard Reset Send\n", (int)get_time().val);
            usb_pd_phy_send_packet(0, NULL, 0, USBPD_SOP_TYPE_HARD_RESET);
            usb_pd_init(port);
        }
        break;

    case PD_STATE_HARD_RESET_EXECUTE:
        /* 执行硬复位状态 — 复位端口状态并重新初始化 */
        if (pd[port].last_state != pd[port].task_state) {
            PDPRINT ("%05d [PD] Hard Reset Execute\n", (int)get_time().val);
            pd[port].message_id = 0;
            pd[port].supply_voltage = 0;
            pd[port].curr_limit = 0;
            pd[port].supply_power = 0;
            pd[port].pdo_idx = 0;
#if CONFIG_USB_PD_ROLE_DEFAULT
            set_state_machine_state (port, PD_STATE_SRC_DISCONNECTED);
#else
            set_state_machine_state (port, PD_STATE_SNK_DISCONNECTED);
#endif
        }
        break;

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
    /* --- 双角色自动切换状态 --- */
    case PD_STATE_DRP_AUTO_TOGGLE:
        /* 双角色自动切换状态 */
        break;
#endif

    default:
        break;
    }
    pd[port].last_state = this_state;

    if (pd[port].timeout) {
        now = get_time();
        if (now.val >= pd[port].timeout) {
            /* SNK_DISCOVERY: 超时后先主动发送 GetSourceCap，而非直接跳转 SOFT_RESET */
            if (this_state == PD_STATE_SNK_DISCOVERY && !pd[port].get_source_cap_sent) {
                pd[port].get_source_cap_sent = 1;
                pd_send_message(port, USBPD_SOP_TYPE_SOP, USBPD_CTRL_MSG_GET_SOURCE_CAP, NULL, 0);
                set_state_machine_timeout(port, get_time().val + USBPD_T_SINK_WAIT_CAP, PD_STATE_SOFT_RESET);
            }
            /* 消息重传：SOFT_RESET 超时 + 尚有重试次数 */
            else if (pd[port].timeout_state == PD_STATE_SOFT_RESET && pd[port].retry_count < pd[port].max_retry) {
                pd[port].retry_count++;
                pd_send_message(port, pd[port].last_sent_sop, pd[port].last_sent_msg_type,
                               pd[port].last_sent_payload, pd[port].last_sent_payload_len);
                set_state_machine_timeout(port, get_time().val + USBPD_T_SENDER_RESPONSE, PD_STATE_SOFT_RESET);
            }
            else {
                set_state_machine_state (port, pd[port].timeout_state);
            }
            timeout = (timeout < (10 * MSEC)) ? timeout : (10 * MSEC);
        } else if ((pd[port].timeout - now.val) < timeout) {
            timeout = pd[port].timeout - now.val;
        }
    }
}