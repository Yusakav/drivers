#include "pd_source.h"
#include "usbpd_message.h"
#include "usbpd_phy_ch32x035.h"
#include "usbpd_protocol.h"
#include <string.h>

#define PD_SOURCE_CC_DEBOUNCE_MS 100U
#define PD_SOURCE_CAP_PERIOD_MS 150U
#define PD_SOURCE_TRANSITION_MS USBPD_T_PS_TRANSITION_SPR_MAX_MS
#define PD_SOURCE_HARD_RESET_MS 35U
#define PD_SOURCE_PPS_TIMEOUT_MS 13500U
#define PD_SOURCE_MAX_CAP_ATTEMPTS 6U

enum pd_source_reply_e
{
    PD_SOURCE_REPLY_NONE = 0U,
    PD_SOURCE_REPLY_ACCEPT,
    PD_SOURCE_REPLY_REJECT,
    PD_SOURCE_REPLY_NOT_SUPPORTED,
    PD_SOURCE_REPLY_SOURCE_CAP_EXT,
    PD_SOURCE_REPLY_SINK_CAP,
    PD_SOURCE_REPLY_SINK_CAP_EXT,
    PD_SOURCE_REPLY_SOURCE_INFO,
    PD_SOURCE_REPLY_DPM_EXTENDED,
    PD_SOURCE_REPLY_REVISION,
};

struct pd_source_context_t
{
    struct pd_source_config_t config;
    struct pd_source_status_t status;
    union usbpd_rdo_u request;
    uint32_t requested_voltage_mv;
    uint32_t requested_current_ma;
    uint32_t deadline_ms;
    uint32_t pps_deadline_ms;
    uint32_t now_ms;
    uint8_t extended_reply[USBPD_EXT_DATA_MAX];
    uint16_t extended_reply_length;
    uint8_t selected_index;
    uint8_t extended_reply_type;
    uint8_t pending_reply;
    uint8_t cap_attempts;
    uint8_t transition_started : 1;
    uint8_t accept_acked : 1;
    uint8_t peer_soft_reset : 1;
    uint8_t soft_reset_sent : 1;
    uint8_t hard_reset_received : 1;
    uint8_t hard_reset_count;
};

static struct pd_source_context_t g_source;

static uint8_t source_deadline(uint32_t now_ms)
{
    return ((int32_t)(now_ms - g_source.deadline_ms) >= 0);
}

static uint8_t source_is_rd(enum usbpd_cc_e cc)
{
    return (cc == USBPD_CC_RD);
}

static void source_set_state(uint8_t state, uint32_t deadline_ms)
{
    g_source.status.state = state;
    g_source.deadline_ms = deadline_ms;
}

static void source_event(uint8_t event, uint32_t value0, uint32_t value1)
{
    if (g_source.config.dpm.event != 0)
        g_source.config.dpm.event(0U, event, value0, value1, g_source.config.dpm.context);
}

static int source_dpm_message(const struct usbpd_protocol_msg_t *msg, uint8_t category)
{
    if ((msg == 0) || (g_source.config.dpm.message_received == 0))
        return USBPD_ERR_UNSUPPORTED;
    return g_source.config.dpm.message_received(0U, msg->sop, category, msg->header.bits.type, msg->payload,
                                                msg->length, g_source.config.dpm.context);
}

static void source_prepare_extended_reply(uint8_t type)
{
    uint16_t length = sizeof(g_source.extended_reply);
    if ((g_source.config.dpm.get_extended != 0) &&
        (g_source.config.dpm.get_extended(0U, type, g_source.extended_reply, &length, g_source.config.dpm.context) ==
         USBPD_OK) &&
        (length != 0U) && (length <= sizeof(g_source.extended_reply)))
    {
        g_source.extended_reply_type = type;
        g_source.extended_reply_length = length;
        g_source.pending_reply = PD_SOURCE_REPLY_DPM_EXTENDED;
    }
    else
    {
        g_source.pending_reply = PD_SOURCE_REPLY_NOT_SUPPORTED;
    }
}

static void source_clear_contract(void)
{
    if (g_source.status.contract_valid != 0U)
        source_event(USBPD_DPM_CONTRACT_LOST, 0U, 0U);
    g_source.status.contract_valid = 0U;
    g_source.status.pps_active = 0U;
    g_source.status.selected_pdo = 0U;
    g_source.status.negotiated_voltage_mv = 0U;
    g_source.status.negotiated_current_ma = 0U;
    g_source.transition_started = 0U;
}

static uint8_t source_config_valid(const struct pd_source_config_t *config)
{
    uint8_t i;
    if ((config == 0) || (config->source_pdo_count == 0U) || (config->source_pdo_count > USBPD_MAX_DATA_OBJ) ||
        (config->sink_pdo_count > USBPD_MAX_DATA_OBJ) || (config->dpm.set_source == 0))
        return 0U;
    if ((config->source_pdo[0].common.type != USBPD_PDO_FIXED) || (config->source_pdo[0].fixed.voltage_50mv != 100U))
        return 0U;
    for (i = 0U; i < config->source_pdo_count; ++i)
    {
        if ((config->source_pdo[i].common.type == USBPD_PDO_FIXED) && (config->source_pdo[i].fixed.epr_capable != 0U))
            return 0U;
        if ((config->source_pdo[i].common.type == USBPD_PDO_APDO) &&
            (config->source_pdo[i].common.subtype == USBPD_APDO_EPR_AVS))
            return 0U;
        if ((config->source_pdo[i].common.type == USBPD_PDO_APDO) &&
            (config->source_pdo[i].common.subtype == USBPD_APDO_PPS) && (config->features.pps == 0U))
            return 0U;
        if ((config->source_pdo[i].common.type == USBPD_PDO_APDO) &&
            (config->source_pdo[i].common.subtype == USBPD_APDO_SPR_AVS) && (config->features.avs == 0U))
            return 0U;
    }
    if ((config->sink_pdo_count != 0U) &&
        ((config->sink_pdo[0].common.type != USBPD_PDO_FIXED) || (config->sink_pdo[0].sink_fixed.voltage_50mv != 100U)))
        return 0U;
    return 1U;
}

static uint8_t source_request_valid(const struct usbpd_protocol_msg_t *msg)
{
    union usbpd_pdo_u pdo;
    uint8_t position;
    uint32_t voltage_mv = 0U;
    uint32_t current_ma = 0U;

    if ((msg == 0) || (msg->length != 4U))
        return 0U;
    memcpy(&g_source.request.raw, msg->payload, sizeof(g_source.request.raw));
    position = g_source.request.fixed.object_position;
    if ((position == 0U) || (position > g_source.config.source_pdo_count))
        return 0U;
    pdo = g_source.config.source_pdo[position - 1U];

    if ((pdo.common.type == USBPD_PDO_FIXED) || (pdo.common.type == USBPD_PDO_VARIABLE))
    {
        uint32_t max_current_ma =
            ((pdo.common.type == USBPD_PDO_FIXED) ? pdo.fixed.current_10ma : pdo.variable.current_10ma) * 10U;
        current_ma = g_source.request.fixed.operating_current_10ma * 10U;
        if ((current_ma == 0U) || (current_ma > max_current_ma) ||
            (g_source.request.fixed.max_current_10ma < g_source.request.fixed.operating_current_10ma) ||
            ((g_source.request.fixed.max_current_10ma * 10U) > max_current_ma))
            return 0U;
        voltage_mv =
            (pdo.common.type == USBPD_PDO_FIXED) ? pdo.fixed.voltage_50mv * 50U : pdo.variable.max_voltage_50mv * 50U;
    }
    else if (pdo.common.type == USBPD_PDO_BATTERY)
    {
        uint32_t power_mw = g_source.request.battery.operating_power_250mw * 250U;
        if ((power_mw == 0U) || (g_source.request.battery.operating_power_250mw > pdo.battery.power_250mw) ||
            (g_source.request.battery.max_power_250mw < g_source.request.battery.operating_power_250mw) ||
            (g_source.request.battery.max_power_250mw > pdo.battery.power_250mw))
            return 0U;
        voltage_mv = pdo.battery.max_voltage_50mv * 50U;
        current_ma = (voltage_mv != 0U) ? (power_mw * 1000U) / voltage_mv : 0U;
    }
    else if ((pdo.common.type == USBPD_PDO_APDO) && (pdo.common.subtype == USBPD_APDO_PPS) &&
             (g_source.config.features.pps != 0U))
    {
        voltage_mv = g_source.request.pps.output_voltage_20mv * 20U;
        current_ma = g_source.request.pps.operating_current_50ma * 50U;
        if ((voltage_mv < pdo.pps.min_voltage_100mv * 100U) || (voltage_mv > pdo.pps.max_voltage_100mv * 100U) ||
            (current_ma == 0U) || (current_ma > pdo.pps.current_50ma * 50U))
            return 0U;
    }
    else if ((pdo.common.type == USBPD_PDO_APDO) && (pdo.common.subtype == USBPD_APDO_SPR_AVS) &&
             (g_source.config.features.avs != 0U))
    {
        uint32_t max_current_ma;
        voltage_mv = g_source.request.avs.output_voltage_25mv * 25U;
        current_ma = g_source.request.avs.operating_current_50ma * 50U;
        max_current_ma = ((voltage_mv <= 15000U) ? pdo.spr_avs.current_15v_10ma : pdo.spr_avs.current_20v_10ma) * 10U;
        if ((voltage_mv < 9000U) || (voltage_mv > 20000U) || (current_ma == 0U) || (current_ma > max_current_ma))
            return 0U;
    }
    else
    {
        return 0U;
    }

    g_source.selected_index = (uint8_t)(position - 1U);
    g_source.requested_voltage_mv = voltage_mv;
    g_source.requested_current_ma = current_ma;
    return 1U;
}

static void source_protocol_event(uint8_t event, const struct usbpd_protocol_msg_t *msg, void *arg)
{
    uint32_t now_ms = g_source.now_ms;
    (void)arg;
    if (event == USBPD_PROTOCOL_TX_GOODCRC)
    {
        if ((msg != 0) && (msg->sop == USBPD_SOP) && (msg->header.bits.data_objects == 0U) &&
            (msg->header.bits.type == USBPD_CTRL_ACCEPT) && (g_source.status.state == PD_SOURCE_TRANSITION))
            g_source.accept_acked = 1U;
        return;
    }
    if (event == USBPD_PROTOCOL_HARD_RESET)
    {
        source_event(USBPD_DPM_HARD_RESET, 0U, 0U);
        source_clear_contract();
        usbpd_protocol_reset();
        g_source.hard_reset_received = 1U;
        source_set_state(PD_SOURCE_HARD_RESET, now_ms);
        return;
    }
    if (event == USBPD_PROTOCOL_SOFT_RESET)
    {
        usbpd_protocol_reset();
        g_source.peer_soft_reset = 1U;
        source_set_state(PD_SOURCE_SOFT_RESET, now_ms + USBPD_T_SENDER_RESPONSE_MAX_MS);
        return;
    }
    if ((event == USBPD_PROTOCOL_TX_TIMEOUT) || (event == USBPD_PROTOCOL_RX_OVERFLOW) ||
        (event == USBPD_PROTOCOL_ERROR))
    {
        usbpd_observer_record((event == USBPD_PROTOCOL_TX_TIMEOUT) ? MESSAGE_BUFFER_TX : MESSAGE_BUFFER_RX,
                              USBPD_SOP_INVALID, 0, 0U, now_ms, USBPD_OBSERVER_ERROR);
        source_event(USBPD_DPM_PROTOCOL_ERROR, event, 0U);
        source_set_state(PD_SOURCE_HARD_RESET, now_ms);
        return;
    }
    if (event == USBPD_PROTOCOL_EXT_RX)
    {
        if ((msg != 0) && (msg->header.bits.type == USBPD_EXT_CONTROL) &&
            (msg->length == sizeof(struct usbpd_extended_control_db_t)))
        {
            const struct usbpd_extended_control_db_t *ecdb = (const struct usbpd_extended_control_db_t *)msg->payload;
            if (ecdb->type == USBPD_EXT_CTRL_EPR_GET_SOURCE_CAP)
                g_source.pending_reply = PD_SOURCE_REPLY_NOT_SUPPORTED;
            else
                (void)source_dpm_message(msg, USBPD_DPM_EXTENDED_MESSAGE);
        }
        else
        {
            (void)source_dpm_message(msg, USBPD_DPM_EXTENDED_MESSAGE);
        }
        return;
    }
    if ((event != USBPD_PROTOCOL_RX) || (msg == 0))
        return;

    if (msg->header.bits.data_objects != 0U)
    {
        if ((msg->sop == USBPD_SOP) && (msg->header.bits.type == USBPD_DATA_REQUEST) &&
            ((g_source.status.state == PD_SOURCE_WAIT_REQUEST) || (g_source.status.state == PD_SOURCE_READY)))
        {
            g_source.pending_reply = source_request_valid(msg) ? PD_SOURCE_REPLY_ACCEPT : PD_SOURCE_REPLY_REJECT;
        }
        else if ((msg->header.bits.type == USBPD_DATA_EPR_REQUEST) || (msg->header.bits.type == USBPD_DATA_EPR_MODE) ||
                 (msg->header.bits.type == USBPD_DATA_ENTER_USB))
        {
            g_source.pending_reply = PD_SOURCE_REPLY_NOT_SUPPORTED;
        }
        else
        {
            (void)source_dpm_message(msg, USBPD_DPM_DATA_MESSAGE);
        }
        return;
    }

    switch (msg->header.bits.type)
    {
    case USBPD_CTRL_ACCEPT:
        if ((g_source.status.state == PD_SOURCE_SOFT_RESET) && (g_source.soft_reset_sent != 0U))
        {
            g_source.soft_reset_sent = 0U;
            source_set_state(PD_SOURCE_SEND_CAPABILITIES, now_ms);
        }
        break;
    case USBPD_CTRL_GET_SOURCE_CAP:
        source_set_state(PD_SOURCE_SEND_CAPABILITIES, now_ms);
        break;
    case USBPD_CTRL_GET_SOURCE_CAP_EXT:
        g_source.pending_reply =
            g_source.config.source_cap_ext_valid ? PD_SOURCE_REPLY_SOURCE_CAP_EXT : PD_SOURCE_REPLY_NOT_SUPPORTED;
        break;
    case USBPD_CTRL_GET_SINK_CAP:
        g_source.pending_reply =
            (g_source.config.sink_pdo_count != 0U) ? PD_SOURCE_REPLY_SINK_CAP : PD_SOURCE_REPLY_NOT_SUPPORTED;
        break;
    case USBPD_CTRL_GET_SINK_CAP_EXT:
        g_source.pending_reply =
            g_source.config.sink_cap_ext_valid ? PD_SOURCE_REPLY_SINK_CAP_EXT : PD_SOURCE_REPLY_NOT_SUPPORTED;
        break;
    case USBPD_CTRL_GET_SOURCE_INFO:
        g_source.pending_reply =
            g_source.config.source_info_valid ? PD_SOURCE_REPLY_SOURCE_INFO : PD_SOURCE_REPLY_NOT_SUPPORTED;
        break;
    case USBPD_CTRL_GET_STATUS:
        source_prepare_extended_reply(USBPD_EXT_STATUS);
        break;
    case USBPD_CTRL_GET_PPS_STATUS:
        source_prepare_extended_reply(USBPD_EXT_PPS_STATUS);
        break;
    case USBPD_CTRL_GET_COUNTRY_CODES:
        source_prepare_extended_reply(USBPD_EXT_COUNTRY_CODES);
        break;
    case USBPD_CTRL_GET_REVISION:
        g_source.pending_reply = PD_SOURCE_REPLY_REVISION;
        break;
    case USBPD_CTRL_DR_SWAP:
    case USBPD_CTRL_PR_SWAP:
    case USBPD_CTRL_VCONN_SWAP:
    case USBPD_CTRL_FR_SWAP:
        g_source.pending_reply = PD_SOURCE_REPLY_REJECT;
        break;
    case USBPD_CTRL_DATA_RESET:
        g_source.pending_reply = PD_SOURCE_REPLY_NOT_SUPPORTED;
        break;
    default:
        (void)source_dpm_message(msg, USBPD_DPM_CONTROL_MESSAGE);
        break;
    }
}

static void source_service_reply(uint32_t now_ms)
{
    int ret = USBPD_BUSY;
    switch (g_source.pending_reply)
    {
    case PD_SOURCE_REPLY_ACCEPT:
        ret = usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_ACCEPT, now_ms);
        if (ret == USBPD_OK)
        {
            g_source.transition_started = 0U;
            g_source.accept_acked = 0U;
            source_set_state(PD_SOURCE_TRANSITION, now_ms + PD_SOURCE_TRANSITION_MS);
        }
        break;
    case PD_SOURCE_REPLY_REJECT:
        ret = usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_REJECT, now_ms);
        break;
    case PD_SOURCE_REPLY_NOT_SUPPORTED:
        ret = usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_NOT_SUPPORTED, now_ms);
        break;
    case PD_SOURCE_REPLY_SOURCE_CAP_EXT:
        ret = usbpd_protocol_send_extended(USBPD_EXT_SOURCE_CAP, (const uint8_t *)&g_source.config.source_cap_ext,
                                           sizeof(g_source.config.source_cap_ext), now_ms);
        break;
    case PD_SOURCE_REPLY_SINK_CAP:
        ret = usbpd_protocol_send_data(USBPD_SOP, USBPD_DATA_SINK_CAP, (const uint32_t *)g_source.config.sink_pdo,
                                       g_source.config.sink_pdo_count, now_ms);
        break;
    case PD_SOURCE_REPLY_SINK_CAP_EXT:
        ret = usbpd_protocol_send_extended(USBPD_EXT_SINK_CAP, (const uint8_t *)&g_source.config.sink_cap_ext,
                                           sizeof(g_source.config.sink_cap_ext), now_ms);
        break;
    case PD_SOURCE_REPLY_SOURCE_INFO:
        ret = usbpd_protocol_send_data(USBPD_SOP, USBPD_DATA_SOURCE_INFO, &g_source.config.source_info.raw, 1U, now_ms);
        break;
    case PD_SOURCE_REPLY_DPM_EXTENDED:
        ret = usbpd_protocol_send_extended(g_source.extended_reply_type, g_source.extended_reply,
                                           g_source.extended_reply_length, now_ms);
        break;
    case PD_SOURCE_REPLY_REVISION: {
        union usbpd_revision_do_u revision;
        revision.raw = 0U;
        revision.bits.revision_major = 3U;
        revision.bits.revision_minor = 2U;
        revision.bits.version_major = 1U;
        revision.bits.version_minor = 1U;
        ret = usbpd_protocol_send_data(USBPD_SOP, USBPD_DATA_REVISION, &revision.raw, 1U, now_ms);
        break;
    }
    default:
        return;
    }
    if (ret == USBPD_OK)
        g_source.pending_reply = PD_SOURCE_REPLY_NONE;
}

uint8_t pd_source_is_attached(void)
{
    return (g_source.status.state != PD_SOURCE_UNATTACHED);
}

int pd_source_get_status(struct pd_source_status_t *status)
{
    if (status == 0)
        return USBPD_ERR_PARAM;
    *status = g_source.status;
    return USBPD_OK;
}

int pd_source_send_control(uint8_t sop, uint8_t type)
{
    return usbpd_protocol_send_ctrl(sop, type, g_source.now_ms);
}

int pd_source_send_data_objects(uint8_t sop, uint8_t type, const uint32_t *objects, uint8_t count)
{
    return usbpd_protocol_send_data(sop, type, objects, count, g_source.now_ms);
}

int pd_source_send_extended(uint8_t sop, uint8_t type, const uint8_t *data, uint16_t length)
{
    return usbpd_protocol_send_extended_sop(sop, type, data, length, g_source.now_ms);
}

int pd_source_init(const struct pd_source_config_t *config)
{
    return pd_source_policy_init(config, 1U);
}

int pd_source_policy_init(const struct pd_source_config_t *config, uint8_t initialize_hardware)
{
    if (source_config_valid(config) == 0U)
        return USBPD_ERR_PARAM;
    memset(&g_source, 0, sizeof(g_source));
    g_source.config = *config;
    if (initialize_hardware != 0U)
    {
        usbpd_observer_init();
        if (usbpd_phy_init() != USBPD_OK)
            return USBPD_ERR;
    }
    usbpd_phy_set_rp(config->source_pdo[0].fixed.current_10ma * 10U);
    usbpd_phy_set_pull(USBPD_CC_PULL_RP);
    usbpd_protocol_init(source_protocol_event, 0);
    usbpd_protocol_configure(USBPD_REV30, USBPD_POWER_ROLE_SOURCE, USBPD_DATA_ROLE_DFP);
    source_set_state(PD_SOURCE_UNATTACHED, 0U);
    return USBPD_OK;
}

void pd_source_task(uint32_t now_ms)
{
    enum usbpd_cc_e cc1;
    enum usbpd_cc_e cc2;
    uint32_t vbus_mv;
    g_source.now_ms = now_ms;
    usbpd_phy_task(now_ms);
    usbpd_protocol_task(now_ms);
    source_service_reply(now_ms);
    usbpd_phy_get_cc(&cc1, &cc2);
    if (usbpd_phy_get_vbus(&vbus_mv) == USBPD_OK)
        g_source.status.vbus_mv = vbus_mv;

    if ((g_source.status.state != PD_SOURCE_UNATTACHED) && (g_source.status.state != PD_SOURCE_ERROR_RECOVERY) &&
        !source_is_rd(cc1) && !source_is_rd(cc2))
    {
        source_clear_contract();
        (void)g_source.config.dpm.set_source(0U, 0U, 0U, 0U, g_source.config.dpm.context);
        g_source.status.attached = 0U;
        source_event(USBPD_DPM_DETACHED, 0U, 0U);
        usbpd_protocol_reset();
        source_set_state(PD_SOURCE_UNATTACHED, 0U);
    }

    switch (g_source.status.state)
    {
    case PD_SOURCE_UNATTACHED:
        if (source_is_rd(cc1) || source_is_rd(cc2))
            source_set_state(PD_SOURCE_ATTACH_WAIT, now_ms + PD_SOURCE_CC_DEBOUNCE_MS);
        break;
    case PD_SOURCE_ATTACH_WAIT:
        if (!source_is_rd(cc1) && !source_is_rd(cc2))
            source_set_state(PD_SOURCE_UNATTACHED, 0U);
        else if (source_deadline(now_ms))
        {
            uint32_t default_ma = g_source.config.source_pdo[0].fixed.current_10ma * 10U;
            usbpd_phy_set_cc(source_is_rd(cc1) ? 1U : 2U);
            if (g_source.config.dpm.set_source(0U, 1U, 5000U, default_ma, g_source.config.dpm.context) == USBPD_OK)
            {
                g_source.status.attached = 1U;
                source_event(USBPD_DPM_ATTACHED, 0U, 0U);
                source_set_state(PD_SOURCE_STARTUP, now_ms + PD_SOURCE_TRANSITION_MS);
            }
            else
            {
                source_set_state(PD_SOURCE_ERROR_RECOVERY, now_ms + PD_SOURCE_HARD_RESET_MS);
            }
        }
        break;
    case PD_SOURCE_STARTUP:
        if ((g_source.config.dpm.source_ready == 0) ||
            (g_source.config.dpm.source_ready(0U, 5000U, g_source.config.dpm.context) != 0U))
        {
            g_source.cap_attempts = 0U;
            source_set_state(PD_SOURCE_SEND_CAPABILITIES, now_ms);
        }
        else if (source_deadline(now_ms))
        {
            source_set_state(PD_SOURCE_ERROR_RECOVERY, now_ms + PD_SOURCE_HARD_RESET_MS);
        }
        break;
    case PD_SOURCE_SEND_CAPABILITIES:
        if (usbpd_protocol_send_data(USBPD_SOP, USBPD_DATA_SOURCE_CAP, (const uint32_t *)g_source.config.source_pdo,
                                     g_source.config.source_pdo_count, now_ms) == USBPD_OK)
        {
            g_source.cap_attempts++;
            source_set_state(PD_SOURCE_WAIT_REQUEST, now_ms + PD_SOURCE_CAP_PERIOD_MS);
        }
        break;
    case PD_SOURCE_WAIT_REQUEST:
        if (source_deadline(now_ms))
        {
            if (g_source.cap_attempts < PD_SOURCE_MAX_CAP_ATTEMPTS)
                source_set_state(PD_SOURCE_SEND_CAPABILITIES, now_ms);
            else
                source_set_state(PD_SOURCE_ERROR_RECOVERY, now_ms + PD_SOURCE_HARD_RESET_MS);
        }
        break;
    case PD_SOURCE_TRANSITION:
        if (g_source.accept_acked == 0U)
        {
            break;
        }
        else if (g_source.transition_started == 0U)
        {
            if (g_source.config.dpm.set_source(0U, 1U, g_source.requested_voltage_mv, g_source.requested_current_ma,
                                               g_source.config.dpm.context) == USBPD_OK)
                g_source.transition_started = 1U;
            else
                source_set_state(PD_SOURCE_HARD_RESET, now_ms);
        }
        else if ((g_source.config.dpm.source_ready == 0) ||
                 (g_source.config.dpm.source_ready(0U, g_source.requested_voltage_mv, g_source.config.dpm.context) !=
                  0U))
        {
            if (usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_PS_RDY, now_ms) == USBPD_OK)
            {
                union usbpd_pdo_u pdo = g_source.config.source_pdo[g_source.selected_index];
                g_source.status.negotiated_voltage_mv = g_source.requested_voltage_mv;
                g_source.status.negotiated_current_ma = g_source.requested_current_ma;
                g_source.status.selected_pdo = (uint8_t)(g_source.selected_index + 1U);
                g_source.status.contract_valid = 1U;
                g_source.status.pps_active =
                    ((pdo.common.type == USBPD_PDO_APDO) && (pdo.common.subtype == USBPD_APDO_PPS));
                g_source.pps_deadline_ms = now_ms + PD_SOURCE_PPS_TIMEOUT_MS;
                g_source.hard_reset_count = 0U;
                source_event(USBPD_DPM_CONTRACT, g_source.status.negotiated_voltage_mv,
                             g_source.status.negotiated_current_ma);
                source_set_state(PD_SOURCE_READY, 0U);
            }
        }
        else if (source_deadline(now_ms))
        {
            source_set_state(PD_SOURCE_HARD_RESET, now_ms);
        }
        break;
    case PD_SOURCE_READY:
        if ((g_source.status.pps_active != 0U) && ((int32_t)(now_ms - g_source.pps_deadline_ms) >= 0))
            source_set_state(PD_SOURCE_HARD_RESET, now_ms);
        break;
    case PD_SOURCE_SOFT_RESET:
        if ((g_source.peer_soft_reset != 0U) &&
            (usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_ACCEPT, now_ms) == USBPD_OK))
        {
            g_source.peer_soft_reset = 0U;
            source_set_state(PD_SOURCE_SEND_CAPABILITIES, now_ms);
        }
        else if ((g_source.peer_soft_reset == 0U) && (g_source.soft_reset_sent == 0U) &&
                 (usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_SOFT_RESET, now_ms) == USBPD_OK))
        {
            g_source.soft_reset_sent = 1U;
            g_source.deadline_ms = now_ms + USBPD_T_SENDER_RESPONSE_MAX_MS;
        }
        else if ((g_source.soft_reset_sent != 0U) && source_deadline(now_ms))
        {
            source_set_state(PD_SOURCE_HARD_RESET, now_ms);
        }
        break;
    case PD_SOURCE_HARD_RESET:
        if ((g_source.hard_reset_received == 0U) && (g_source.hard_reset_count < USBPD_N_HARD_RESET_COUNT))
        {
            (void)usbpd_protocol_send_hard_reset(now_ms);
            g_source.hard_reset_count++;
        }
        g_source.hard_reset_received = 0U;
        source_event(USBPD_DPM_HARD_RESET, 0U, 0U);
        source_clear_contract();
        (void)g_source.config.dpm.set_source(0U, 0U, 0U, 0U, g_source.config.dpm.context);
        if (g_source.config.dpm.set_discharge != 0)
            (void)g_source.config.dpm.set_discharge(0U, 1U, g_source.config.dpm.context);
        usbpd_protocol_reset();
        source_set_state(PD_SOURCE_ERROR_RECOVERY, now_ms + PD_SOURCE_HARD_RESET_MS);
        break;
    case PD_SOURCE_ERROR_RECOVERY:
        if (source_deadline(now_ms))
        {
            if (g_source.config.dpm.set_discharge != 0)
                (void)g_source.config.dpm.set_discharge(0U, 0U, g_source.config.dpm.context);
            usbpd_protocol_reset();
            source_set_state(PD_SOURCE_UNATTACHED, 0U);
        }
        break;
    default:
        source_set_state(PD_SOURCE_ERROR_RECOVERY, now_ms + PD_SOURCE_HARD_RESET_MS);
        break;
    }
}
