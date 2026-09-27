#include "pd_sink.h"
#include "usbpd_phy_ch32x035.h"
#include "usbpd_protocol.h"
#include "usbpd_message.h"
#include <string.h>

#define PD_SINK_CC_DEBOUNCE_MS 100U
#define PD_SINK_WAIT_CAP_MS USBPD_T_SINK_WAIT_CAP_MIN_MS
#define PD_SINK_SENDER_RESPONSE_MS USBPD_T_SENDER_RESPONSE_MAX_MS
#define PD_SINK_PS_TRANSITION_MS USBPD_T_PS_TRANSITION_SPR_MS
#define PD_SINK_ERROR_RECOVERY_MS 30U
#define PD_SINK_EPR_KEEPALIVE_MS USBPD_T_SINK_EPR_KEEPALIVE_MS
#define PD_SINK_MAX_WAIT_RETRIES 2U

enum pd_sink_select_e
{
    PD_SELECT_NONE = 0U,
    PD_SELECT_FIXED,
    PD_SELECT_VARIABLE,
    PD_SELECT_BATTERY,
    PD_SELECT_PPS,
    PD_SELECT_AVS
};
enum pd_sink_reply_e
{
    PD_REPLY_NONE = 0U,
    PD_REPLY_SINK_CAP,
    PD_REPLY_SINK_CAP_EXT,
    PD_REPLY_EPR_SINK_CAP,
    PD_REPLY_REVISION,
    PD_REPLY_EXTENDED,
    PD_REPLY_REJECT,
    PD_REPLY_VDM_NAK,
    PD_REPLY_NOT_SUPPORTED
};

struct pd_sink_t
{
    struct pd_sink_config_t config;
    struct pd_sink_status_t status;
    union usbpd_pdo_u source_pdo[USBPD_MAX_DATA_OBJ];
    union usbpd_pdo_u epr_source_pdo[USBPD_MAX_EPR_DATA_OBJ];
    uint8_t source_count;
    uint8_t epr_source_count;
    uint8_t select_kind : 3;
    uint8_t source_valid : 1;
    uint8_t epr_caps_valid : 1;
    uint8_t target_dirty : 1;
    uint8_t get_source_cap_sent : 1;
    uint8_t get_epr_cap_sent : 1;
    uint8_t epr_mode_sent : 1;
    uint8_t epr_enter_acked : 1;
    uint8_t epr_keepalive_pending : 1;
    uint8_t epr_target_pending : 1;
    uint8_t epr_exit_pending : 1;
    uint8_t peer_soft_reset : 1;
    uint8_t soft_reset_sent : 1;
    uint8_t wait_pending : 1;
    uint8_t pending_reply;
    uint8_t wait_retries : 2;
    uint8_t hard_reset_count : 2;
    uint8_t selected_index;
    uint8_t pending_vdm_sop;
    uint32_t pending_vdm;
    uint8_t pending_ext_type;
    uint16_t pending_ext_length;
    uint8_t pending_ext_data[USBPD_EXT_DATA_MAX];
    uint32_t selected_voltage_mv;
    uint32_t selected_current_ma;
    uint32_t deadline_ms;
    uint32_t epr_keepalive_ms;
    uint32_t pps_request_ms;
    uint32_t now_ms;
};

static struct pd_sink_t g_sink;

static uint8_t sink_deadline(uint32_t now)
{
    return ((int32_t)(now - g_sink.deadline_ms) >= 0);
}
static uint32_t sink_min(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}
static uint8_t sink_is_rp(enum usbpd_cc_e cc)
{
    return cc >= USBPD_CC_RP_DEF;
}

static void sink_dpm_event(uint8_t event, uint32_t value0, uint32_t value1)
{
    if (g_sink.config.dpm.event != 0)
        g_sink.config.dpm.event(0U, event, value0, value1, g_sink.config.dpm.context);
}

static int sink_dpm_message(const struct usbpd_protocol_msg_t *msg, uint8_t category)
{
    if ((msg == 0) || (g_sink.config.dpm.message_received == 0))
        return USBPD_ERR_UNSUPPORTED;
    return g_sink.config.dpm.message_received(0U, msg->sop, category, msg->header.bits.type, msg->payload, msg->length,
                                              g_sink.config.dpm.context);
}

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

static void sink_set_state(uint8_t state, uint32_t deadline)
{
    g_sink.status.state = state;
    g_sink.deadline_ms = deadline;
}

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

static int sink_send_data(uint8_t type, const uint32_t *objects, uint8_t count, uint32_t now_ms)
{
    return usbpd_protocol_send_data(USBPD_SOP, type, objects, count, now_ms);
}

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

static uint8_t sink_epr_caps_are_valid(const uint8_t *payload, uint8_t count)
{
    union usbpd_pdo_u first;
    if ((payload == 0) || (count < USBPD_MAX_DATA_OBJ) || (count > USBPD_MAX_EPR_DATA_OBJ))
        return 0U;
    memcpy(first.bytes, payload, sizeof(first.bytes));
    return ((first.common.type == USBPD_PDO_FIXED) && (first.fixed.voltage_50mv == 100U) &&
            (first.fixed.epr_capable != 0U));
}

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

uint8_t pd_sink_is_attached(void)
{
    return (g_sink.status.state != PD_SINK_UNATTACHED);
}

int pd_sink_request(uint32_t voltage_mv, uint32_t current_ma)
{
    if (sink_target_allowed(voltage_mv, current_ma) == 0U)
        return USBPD_ERR;
    g_sink.status.requested_voltage_mv = voltage_mv;
    g_sink.status.requested_current_ma = current_ma;
    g_sink.target_dirty = 1U;
    return USBPD_OK;
}

int pd_sink_get_status(struct pd_sink_status_t *status)
{
    if (status == 0)
        return USBPD_ERR;
    *status = g_sink.status;
    return USBPD_OK;
}

int pd_sink_send_control(uint8_t sop, uint8_t type)
{
    if ((sop > USBPD_SOP_DPRIME) || (type > USBPD_CTRL_GET_REVISION))
        return USBPD_ERR_PARAM;
    return usbpd_protocol_send_ctrl(sop, type, g_sink.now_ms);
}

int pd_sink_send_data_objects(uint8_t sop, uint8_t type, const uint32_t *objects, uint8_t count)
{
    if ((sop > USBPD_SOP_DPRIME) || (type > USBPD_DATA_VENDOR_DEFINED))
        return USBPD_ERR_PARAM;
    return usbpd_protocol_send_data(sop, type, objects, count, g_sink.now_ms);
}

int pd_sink_send_extended(uint8_t sop, uint8_t type, const uint8_t *data, uint16_t length)
{
    if ((sop > USBPD_SOP_DPRIME) || (type > USBPD_EXT_VENDOR_DEFINED))
        return USBPD_ERR_PARAM;
    return usbpd_protocol_send_extended_sop(sop, type, data, length, g_sink.now_ms);
}

int pd_sink_init(const struct pd_sink_config_t *config)
{
    return pd_sink_policy_init(config, 1U);
}

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
