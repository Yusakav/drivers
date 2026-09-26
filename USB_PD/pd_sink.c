#include "pd_sink.h"
#include "usbpd_device.h"
#include "usbpd_protocol.h"
#include "pd_trace.h"
#include <string.h>

#define PD_SINK_CC_DEBOUNCE_MS       100U
#define PD_SINK_WAIT_CAP_MS          310U
#define PD_SINK_SENDER_RESPONSE_MS    30U
#define PD_SINK_PS_TRANSITION_MS     500U
#define PD_SINK_ERROR_RECOVERY_MS     30U
#define PD_SINK_EPR_KEEPALIVE_MS     875U
#define PD_SINK_MAX_WAIT_RETRIES       2U
#define PD_SINK_VDM_DISCOVER_ID      0x0000FF00UL | (1UL << 15) | (1UL << 13) | 1UL
#define PD_SINK_EPR_MODE_ENTER       1UL
#define PD_SINK_EPR_MODE_EXIT        2UL
#define PD_SINK_EPR_MODE_KEEPALIVE   3UL

enum pd_sink_select_e { PD_SELECT_NONE = 0U, PD_SELECT_FIXED, PD_SELECT_PPS, PD_SELECT_AVS };
enum pd_sink_reply_e { PD_REPLY_NONE = 0U, PD_REPLY_SINK_CAP, PD_REPLY_EPR_SINK_CAP, PD_REPLY_NOT_SUPPORTED };

struct pd_sink_t {
    struct pd_sink_config_t config;
    struct pd_sink_status_t status;
    union usbpd_pdo_u source_pdo[USBPD_MAX_DATA_OBJ];
    union usbpd_pdo_u epr_source_pdo[USBPD_MAX_DATA_OBJ];
    uint8_t source_count;
    uint8_t epr_source_count;
    uint8_t select_kind : 2;
    uint8_t source_valid : 1;
    uint8_t epr_caps_valid : 1;
    uint8_t target_dirty : 1;
    uint8_t cable_discovery_sent : 1;
    uint8_t get_source_cap_sent : 1;
    uint8_t get_epr_cap_sent : 1;
    uint8_t epr_mode_sent : 1;
    uint8_t epr_mode_accepted : 1;
    uint8_t epr_mode_action : 2;
    uint8_t peer_soft_reset : 1;
    uint8_t pending_reply : 2;
    uint8_t wait_retries : 2;
    uint8_t selected_index;
    uint32_t selected_voltage_mv;
    uint32_t selected_current_ma;
    uint32_t deadline_ms;
    uint32_t epr_keepalive_ms;
    uint32_t now_ms;
};

static struct pd_sink_t g_sink;

static uint8_t sink_deadline(uint32_t now) { return ((int32_t)(now - g_sink.deadline_ms) >= 0); }
static uint32_t sink_min(uint32_t a, uint32_t b) { return (a < b) ? a : b; }
static uint8_t sink_is_rp(enum usbpd_cc_e cc) { return cc >= USBPD_CC_RP_DEF; }

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
}

static void sink_default_config(struct pd_sink_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->max_voltage_mv = 5000U;
    config->max_current_ma = 1000U;
    config->max_power_mw = 5000U;
    config->sink_pdo_count = 1U;
    config->sink_pdo[0].raw = 0U;
    config->sink_pdo[0].fixed.current_10ma = 100U;
    config->sink_pdo[0].fixed.voltage_50mv = 100U;
    config->sink_pdo[0].fixed.type = USBPD_PDO_FIXED;
}

static uint8_t sink_pdo_allows_target(const union usbpd_pdo_u *pdo, uint8_t count,
                                      uint32_t voltage_mv, uint32_t current_ma,
                                      uint8_t epr)
{
    uint8_t i;

    for (i = 0U; i < count; ++i) {
        if ((pdo[i].common.type == USBPD_PDO_FIXED) && (epr == 0U)) {
            if ((voltage_mv == pdo[i].fixed.voltage_50mv * 50U) &&
                (current_ma <= pdo[i].fixed.current_10ma * 10U)) {
                return 1U;
            }
        } else if ((pdo[i].common.type == USBPD_PDO_APDO) &&
                   (pdo[i].pps.subtype == USBPD_APDO_PPS) &&
                   (epr == 0U) && (g_sink.config.features.pps != 0U)) {
            if ((voltage_mv >= pdo[i].pps.min_voltage_100mv * 100U) &&
                (voltage_mv <= pdo[i].pps.max_voltage_100mv * 100U) &&
                (current_ma <= pdo[i].pps.current_50ma * 50U)) {
                return 1U;
            }
        } else if ((pdo[i].common.type == USBPD_PDO_APDO) &&
                   (pdo[i].avs.subtype == USBPD_APDO_EPR_AVS) && (epr != 0U)) {
            if ((voltage_mv >= pdo[i].avs.min_voltage_100mv * 100U) &&
                (voltage_mv <= pdo[i].avs.max_voltage_100mv * 100U) &&
                (current_ma <= ((uint32_t)pdo[i].avs.pdp_w * 1000U) / voltage_mv)) {
                return 1U;
            }
        }
    }

    return 0U;
}

static uint8_t sink_target_allowed(uint32_t voltage_mv, uint32_t current_ma)
{
    uint64_t power = (uint64_t)voltage_mv * current_ma;
    if ((voltage_mv == 0U) || (current_ma == 0U) ||
        (voltage_mv > g_sink.config.max_voltage_mv) ||
        (current_ma > g_sink.config.max_current_ma) ||
        (power > (uint64_t)g_sink.config.max_power_mw * 1000U)) return 0U;
    if (voltage_mv > 20000U) {
        if (g_sink.config.features.epr == 0U) return 0U;
        return sink_pdo_allows_target(g_sink.config.epr_sink_pdo,
                                      g_sink.config.epr_sink_pdo_count,
                                      voltage_mv, current_ma, 1U);
    }
    return sink_pdo_allows_target(g_sink.config.sink_pdo, g_sink.config.sink_pdo_count,
                                  voltage_mv, current_ma, 0U);
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
    if (g_sink.select_kind == PD_SELECT_FIXED) {
        rdo.fixed.max_current_10ma = (uint16_t)(g_sink.selected_current_ma / 10U);
        rdo.fixed.operating_current_10ma = (uint16_t)(g_sink.selected_current_ma / 10U);
        rdo.fixed.epr_capable = (g_sink.config.features.epr != 0U);
        rdo.fixed.unchunked = 1U;
        rdo.fixed.no_suspend = 1U;
        rdo.fixed.usb_comm = g_sink.config.features.usb_communications;
        rdo.fixed.object_position = (uint8_t)(g_sink.selected_index + 1U);
    } else if (g_sink.select_kind == PD_SELECT_PPS) {
        rdo.pps.operating_current_50ma = (uint8_t)(g_sink.selected_current_ma / 50U);
        rdo.pps.output_voltage_20mv = (uint16_t)(g_sink.selected_voltage_mv / 20U);
        rdo.pps.epr_capable = (g_sink.config.features.epr != 0U);
        rdo.pps.unchunked = 1U;
        rdo.pps.no_suspend = 1U;
        rdo.pps.usb_comm = g_sink.config.features.usb_communications;
        rdo.pps.object_position = (uint8_t)(g_sink.selected_index + 1U);
    } else if (g_sink.select_kind == PD_SELECT_AVS) {
        rdo.avs.operating_current_50ma = (uint8_t)(g_sink.selected_current_ma / 50U);
        rdo.avs.output_voltage_25mv = (uint16_t)(g_sink.selected_voltage_mv / 25U);
        rdo.avs.epr_capable = 1U;
        rdo.avs.unchunked = 1U;
        rdo.avs.no_suspend = 1U;
        rdo.avs.usb_comm = g_sink.config.features.usb_communications;
        rdo.avs.object_position = (uint8_t)(g_sink.selected_index + 1U);
    } else return USBPD_ERR;
    ret = sink_send_data(USBPD_DATA_REQUEST, &rdo.raw, 1U, now_ms);
    if (ret == USBPD_OK) sink_set_state(PD_SINK_REQUESTED, now_ms + PD_SINK_SENDER_RESPONSE_MS);
    return ret;
}

static void sink_service_reply(uint32_t now_ms)
{
    int ret = USBPD_BUSY;
    switch (g_sink.pending_reply) {
    case PD_REPLY_SINK_CAP:
        if (g_sink.config.sink_pdo_count != 0U) {
            ret = sink_send_data(USBPD_DATA_SINK_CAP, (const uint32_t *)g_sink.config.sink_pdo,
                                 g_sink.config.sink_pdo_count, now_ms);
        } else ret = USBPD_ERR;
        break;
    case PD_REPLY_EPR_SINK_CAP:
        if (g_sink.config.epr_sink_pdo_count != 0U) {
            ret = usbpd_protocol_send_extended(18U, (const uint8_t *)g_sink.config.epr_sink_pdo,
                                               (uint16_t)g_sink.config.epr_sink_pdo_count * 4U, now_ms);
        } else {
            g_sink.pending_reply = PD_REPLY_NOT_SUPPORTED;
            return;
        }
        break;
    case PD_REPLY_NOT_SUPPORTED:
        ret = usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_NOT_SUPPORTED, now_ms);
        break;
    default:
        return;
    }
    if (ret == USBPD_OK) g_sink.pending_reply = PD_REPLY_NONE;
}

static int sink_choose_from(const union usbpd_pdo_u *pdo, uint8_t count, uint8_t epr)
{
    uint8_t i;
    for (i = 0U; i < count; ++i) {
        uint32_t voltage;
        uint32_t current;
        if ((pdo[i].common.type == USBPD_PDO_FIXED) && (epr == 0U)) {
            voltage = pdo[i].fixed.voltage_50mv * 50U;
            current = pdo[i].fixed.current_10ma * 10U;
            if ((voltage == g_sink.status.requested_voltage_mv) && (current >= g_sink.status.requested_current_ma)) {
                g_sink.select_kind = PD_SELECT_FIXED;
                g_sink.selected_index = i;
                g_sink.selected_voltage_mv = voltage;
                g_sink.selected_current_ma = g_sink.status.requested_current_ma;
                return USBPD_OK;
            }
        } else if ((pdo[i].common.type == USBPD_PDO_APDO) && (pdo[i].pps.subtype == USBPD_APDO_PPS) &&
                   (epr == 0U) && (g_sink.config.features.pps != 0U)) {
            uint32_t min_mv = pdo[i].pps.min_voltage_100mv * 100U;
            uint32_t max_mv = pdo[i].pps.max_voltage_100mv * 100U;
            current = pdo[i].pps.current_50ma * 50U;
            if ((g_sink.status.requested_voltage_mv >= min_mv) &&
                (g_sink.status.requested_voltage_mv <= max_mv) &&
                (current >= g_sink.status.requested_current_ma)) {
                g_sink.select_kind = PD_SELECT_PPS;
                g_sink.selected_index = i;
                g_sink.selected_voltage_mv = (g_sink.status.requested_voltage_mv / 20U) * 20U;
                g_sink.selected_current_ma = (g_sink.status.requested_current_ma / 50U) * 50U;
                return USBPD_OK;
            }
        } else if ((pdo[i].common.type == USBPD_PDO_APDO) && (pdo[i].avs.subtype == USBPD_APDO_EPR_AVS) && (epr != 0U)) {
            uint32_t min_mv = pdo[i].avs.min_voltage_100mv * 100U;
            uint32_t max_mv = pdo[i].avs.max_voltage_100mv * 100U;
            current = sink_min(g_sink.config.max_current_ma,
                               (pdo[i].avs.pdp_w * 1000U) / g_sink.status.requested_voltage_mv);
            if ((g_sink.status.requested_voltage_mv >= min_mv) &&
                (g_sink.status.requested_voltage_mv <= max_mv) &&
                (current >= g_sink.status.requested_current_ma)) {
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
    for (i = 0U; i < g_sink.source_count; ++i) {
        if ((g_sink.source_pdo[i].common.type == USBPD_PDO_FIXED) &&
            (g_sink.source_pdo[i].fixed.voltage_50mv == 100U)) {
            g_sink.select_kind = PD_SELECT_FIXED;
            g_sink.selected_index = i;
            g_sink.selected_voltage_mv = 5000U;
            g_sink.selected_current_ma = sink_min(g_sink.config.max_current_ma,
                sink_min(g_sink.status.requested_current_ma, g_sink.source_pdo[i].fixed.current_10ma * 10U));
            g_sink.status.fallback_active = 1U;
            return (g_sink.selected_current_ma != 0U) ? USBPD_OK : USBPD_ERR;
        }
    }
    return USBPD_ERR;
}

static void sink_begin_selection(uint32_t now_ms)
{
    uint8_t needs_cable = (g_sink.status.requested_voltage_mv > 20000U) ||
                          (g_sink.status.requested_current_ma > 3000U);
    g_sink.status.fallback_active = 0U;
    if ((g_sink.status.epr_active != 0U) && (g_sink.status.requested_voltage_mv <= 20000U)) {
        g_sink.epr_mode_sent = 0U;
        g_sink.epr_mode_accepted = 0U;
        g_sink.epr_mode_action = PD_SINK_EPR_MODE_EXIT;
        sink_set_state(PD_SINK_EPR_ENTER, now_ms + PD_SINK_SENDER_RESPONSE_MS);
        return;
    }
    if ((needs_cable != 0U) && (g_sink.config.features.require_5a_cable != 0U) &&
        (g_sink.status.cable_5a == 0U)) {
        g_sink.cable_discovery_sent = 0U;
        sink_set_state(PD_SINK_CABLE_DISCOVERY, now_ms + PD_SINK_SENDER_RESPONSE_MS);
        return;
    }
    if (g_sink.status.requested_voltage_mv > 20000U) {
        if (g_sink.epr_caps_valid == 0U) {
            g_sink.get_epr_cap_sent = 0U;
            sink_set_state(PD_SINK_EPR_CAPS, now_ms + PD_SINK_SENDER_RESPONSE_MS);
            return;
        }
        if (sink_choose_from(g_sink.epr_source_pdo, g_sink.epr_source_count, 1U) == USBPD_OK) {
            g_sink.epr_mode_sent = 0U;
            g_sink.epr_mode_accepted = 0U;
            g_sink.epr_mode_action = PD_SINK_EPR_MODE_ENTER;
            sink_set_state(PD_SINK_EPR_ENTER, now_ms + PD_SINK_SENDER_RESPONSE_MS);
            return;
        }
    } else if (sink_choose_from(g_sink.source_pdo, g_sink.source_count, 0U) == USBPD_OK) {
        (void)sink_send_request(now_ms);
        return;
    }
    if (sink_choose_5v() == USBPD_OK) (void)sink_send_request(now_ms);
    else sink_set_state(PD_SINK_ERROR_RECOVERY, now_ms + PD_SINK_ERROR_RECOVERY_MS);
}

static void sink_handle_source_caps(const struct usbpd_protocol_msg_t *msg, uint32_t now_ms)
{
    uint8_t count;
    if ((msg == 0) || (msg->length == 0U) || (msg->length > USBPD_MAX_DATA_OBJ * 4U)) return;
    count = (uint8_t)(msg->length / 4U);
    memcpy(g_sink.source_pdo, msg->payload, msg->length);
    g_sink.source_count = count;
    g_sink.status.source_pdo_count = count;
    g_sink.source_valid = 1U;
    if ((g_sink.status.state == PD_SINK_DISCOVERY) || (g_sink.status.state == PD_SINK_READY)) sink_begin_selection(now_ms);
}

static uint8_t sink_cable_is_5a(const struct usbpd_protocol_msg_t *msg)
{
    const uint32_t *vdo;
    uint8_t count;
    if ((msg == 0) || (msg->length < 20U)) return 0U;
    vdo = (const uint32_t *)msg->payload;
    count = (uint8_t)(msg->length / 4U);
    if (((vdo[0] >> 6) & 0x03U) != 1U) return 0U;
    return (((vdo[count - 1U] >> 5) & 0x03U) >= 2U);
}

static void sink_protocol_event(uint8_t event, const struct usbpd_protocol_msg_t *msg, void *arg)
{
    uint32_t now_ms = g_sink.now_ms;
    (void)arg;
    if (event == USBPD_PROTOCOL_HARD_RESET) {
        sink_reset_contract();
        usbpd_protocol_reset();
        sink_set_state(PD_SINK_ERROR_RECOVERY, now_ms + PD_SINK_ERROR_RECOVERY_MS);
        return;
    }
    if (event == USBPD_PROTOCOL_SOFT_RESET) {
        usbpd_protocol_reset();
        sink_reset_contract();
        g_sink.peer_soft_reset = 1U;
        sink_set_state(PD_SINK_SOFT_RESET, now_ms + PD_SINK_SENDER_RESPONSE_MS);
        return;
    }
    if ((event == USBPD_PROTOCOL_TX_TIMEOUT) ||
        (event == USBPD_PROTOCOL_RX_OVERFLOW)) {
        sink_reset_contract();
        sink_set_state(PD_SINK_HARD_RESET, now_ms + PD_SINK_SENDER_RESPONSE_MS);
        return;
    }
    if (event == USBPD_PROTOCOL_EXT_RX) {
        if ((msg != 0) && (msg->header.bits.type == 17U)) {
            g_sink.epr_source_count = (uint8_t)(msg->length / 4U);
            if (g_sink.epr_source_count > USBPD_MAX_DATA_OBJ) g_sink.epr_source_count = USBPD_MAX_DATA_OBJ;
            memcpy(g_sink.epr_source_pdo, msg->payload, g_sink.epr_source_count * 4U);
            g_sink.epr_caps_valid = 1U;
            g_sink.get_epr_cap_sent = 0U;
        }
        return;
    }
    if ((event != USBPD_PROTOCOL_RX) || (msg == 0)) return;
    if ((msg->sop == USBPD_SOP_PRIME) && (msg->header.bits.type == USBPD_DATA_VENDOR_DEFINED) &&
        (g_sink.status.state == PD_SINK_CABLE_DISCOVERY)) {
        g_sink.status.cable_5a = sink_cable_is_5a(msg);
        if (g_sink.status.cable_5a == 0U) g_sink.status.fallback_active = 1U;
        return;
    }
    if (msg->header.bits.data_objects != 0U) {
        if ((msg->sop == USBPD_SOP) && (msg->header.bits.type == USBPD_DATA_SOURCE_CAP)) sink_handle_source_caps(msg, now_ms);
        return;
    }
    switch (msg->header.bits.type) {
    case USBPD_CTRL_ACCEPT:
        if (g_sink.status.state == PD_SINK_EPR_ENTER) {
            g_sink.epr_mode_accepted = 1U;
        } else if (g_sink.status.state == PD_SINK_REQUESTED) {
            sink_set_state(PD_SINK_TRANSITION, now_ms + PD_SINK_PS_TRANSITION_MS);
        }
        break;
    case USBPD_CTRL_PS_RDY:
        if (g_sink.status.state == PD_SINK_TRANSITION) {
            g_sink.status.contract_valid = 1U;
            g_sink.status.negotiated_voltage_mv = g_sink.selected_voltage_mv;
            g_sink.status.negotiated_current_ma = g_sink.selected_current_ma;
            g_sink.status.selected_pdo = (uint8_t)(g_sink.selected_index + 1U);
            g_sink.status.epr_active = (g_sink.select_kind == PD_SELECT_AVS);
            g_sink.status.fallback_active = 0U;
            g_sink.target_dirty = 0U;
            g_sink.epr_keepalive_ms = now_ms + PD_SINK_EPR_KEEPALIVE_MS;
            sink_set_state(PD_SINK_READY, 0U);
        }
        break;
    case USBPD_CTRL_REJECT:
        if (sink_choose_5v() == USBPD_OK) (void)sink_send_request(now_ms);
        break;
    case USBPD_CTRL_WAIT:
        if (g_sink.wait_retries++ < PD_SINK_MAX_WAIT_RETRIES) sink_set_state(PD_SINK_DISCOVERY, now_ms + PD_SINK_WAIT_CAP_MS);
        else if (sink_choose_5v() == USBPD_OK) (void)sink_send_request(now_ms);
        break;
    case USBPD_CTRL_GET_SINK_CAP:
        g_sink.pending_reply = PD_REPLY_SINK_CAP;
        break;
    case USBPD_CTRL_GET_SINK_CAP_EXT:
        g_sink.pending_reply = PD_REPLY_EPR_SINK_CAP;
        break;
    default:
        g_sink.pending_reply = PD_REPLY_NOT_SUPPORTED;
        break;
    }
}

int pd_sink_init(const struct pd_sink_config_t *config)
{
    struct pd_sink_config_t defaults;
    memset(&g_sink, 0, sizeof(g_sink));
    sink_default_config(&defaults);
    g_sink.config = (config != 0) ? *config : defaults;
    if ((g_sink.config.max_voltage_mv == 0U) || (g_sink.config.max_current_ma == 0U) ||
        (g_sink.config.max_power_mw == 0U) || (g_sink.config.sink_pdo_count > USBPD_MAX_DATA_OBJ) ||
        (g_sink.config.epr_sink_pdo_count > USBPD_MAX_DATA_OBJ)) return USBPD_ERR;
    g_sink.status.requested_voltage_mv = 5000U;
    g_sink.status.requested_current_ma = sink_min(1000U, g_sink.config.max_current_ma);
    usbpd_protocol_init(sink_protocol_event, 0);
    pd_trace_reset();
    if (usbpd_device_init() != USBPD_OK) return USBPD_ERR;
    sink_set_state(PD_SINK_UNATTACHED, 0U);
    return USBPD_OK;
}

int pd_sink_request(uint32_t voltage_mv, uint32_t current_ma)
{
    if (sink_target_allowed(voltage_mv, current_ma) == 0U) return USBPD_ERR;
    g_sink.status.requested_voltage_mv = voltage_mv;
    g_sink.status.requested_current_ma = current_ma;
    g_sink.target_dirty = 1U;
    return USBPD_OK;
}

int pd_sink_get_status(struct pd_sink_status_t *status)
{
    if (status == 0) return USBPD_ERR;
    *status = g_sink.status;
    return USBPD_OK;
}

void pd_sink_task(uint32_t now_ms)
{
    enum usbpd_cc_e cc1;
    enum usbpd_cc_e cc2;
    uint32_t vbus;
    g_sink.now_ms = now_ms;
    usbpd_device_task(now_ms);
    usbpd_protocol_task(now_ms);
    sink_service_reply(now_ms);
    if (usbpd_device_get_vbus(&vbus) == USBPD_OK) g_sink.status.vbus_mv = vbus;
    usbpd_device_get_cc(&cc1, &cc2);
    if (((g_sink.status.state != PD_SINK_UNATTACHED) && (g_sink.status.state != PD_SINK_ERROR_RECOVERY)) &&
        (!sink_is_rp(cc1) && !sink_is_rp(cc2))) {
        sink_reset_contract();
        g_sink.source_valid = 0U;
        g_sink.epr_caps_valid = 0U;
        sink_set_state(PD_SINK_UNATTACHED, 0U);
    }
    switch (g_sink.status.state) {
    case PD_SINK_UNATTACHED:
        if (sink_is_rp(cc1) || sink_is_rp(cc2)) sink_set_state(PD_SINK_ATTACH_WAIT, now_ms + PD_SINK_CC_DEBOUNCE_MS);
        break;
    case PD_SINK_ATTACH_WAIT:
        if (!sink_is_rp(cc1) && !sink_is_rp(cc2)) sink_set_state(PD_SINK_UNATTACHED, 0U);
        else if (sink_deadline(now_ms)) {
            usbpd_device_set_cc(sink_is_rp(cc1) ? 1U : 2U);
            sink_set_state(PD_SINK_DISCOVERY, now_ms + PD_SINK_WAIT_CAP_MS);
            if (g_sink.source_valid != 0U) sink_begin_selection(now_ms);
        }
        break;
    case PD_SINK_DISCOVERY:
        if (g_sink.source_valid != 0U) sink_begin_selection(now_ms);
        else if (sink_deadline(now_ms)) {
            if (g_sink.get_source_cap_sent == 0U) {
                if (usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_GET_SOURCE_CAP, now_ms) == USBPD_OK) {
                    g_sink.get_source_cap_sent = 1U;
                    g_sink.deadline_ms = now_ms + PD_SINK_WAIT_CAP_MS;
                }
            } else sink_set_state(PD_SINK_SOFT_RESET, now_ms);
        }
        break;
    case PD_SINK_CABLE_DISCOVERY:
        if (g_sink.status.cable_5a != 0U) {
            sink_begin_selection(now_ms);
        } else if (g_sink.cable_discovery_sent == 0U) {
            if (usbpd_protocol_send_data(USBPD_SOP_PRIME, USBPD_DATA_VENDOR_DEFINED,
                (const uint32_t[]){ PD_SINK_VDM_DISCOVER_ID }, 1U, now_ms) == USBPD_OK) {
                g_sink.cable_discovery_sent = 1U;
                g_sink.deadline_ms = now_ms + PD_SINK_SENDER_RESPONSE_MS;
            }
        } else if (sink_deadline(now_ms)) {
            g_sink.status.fallback_active = 1U;
            if (sink_choose_5v() == USBPD_OK) (void)sink_send_request(now_ms);
            else sink_set_state(PD_SINK_ERROR_RECOVERY, now_ms + PD_SINK_ERROR_RECOVERY_MS);
        }
        break;
    case PD_SINK_EPR_CAPS:
        if (g_sink.epr_caps_valid != 0U) sink_begin_selection(now_ms);
        else if ((g_sink.get_epr_cap_sent == 0U) &&
                 (usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_GET_SOURCE_CAP_EXT, now_ms) == USBPD_OK))
            g_sink.get_epr_cap_sent = 1U;
        else if (sink_deadline(now_ms)) { if (sink_choose_5v() == USBPD_OK) (void)sink_send_request(now_ms); }
        break;
    case PD_SINK_EPR_ENTER:
        if (g_sink.epr_mode_accepted != 0U) {
            g_sink.epr_mode_accepted = 0U;
            if (g_sink.epr_mode_action == PD_SINK_EPR_MODE_EXIT) {
                g_sink.status.epr_active = 0U;
                sink_begin_selection(now_ms);
            } else {
                (void)sink_send_request(now_ms);
            }
        } else if (g_sink.epr_mode_sent == 0U) {
            if (sink_send_data(USBPD_DATA_EPR_MODE, (const uint32_t[]){ (uint32_t)g_sink.epr_mode_action << 24 }, 1U, now_ms) == USBPD_OK)
                g_sink.epr_mode_sent = 1U;
        } else if (sink_deadline(now_ms)) {
            if (sink_choose_5v() == USBPD_OK) (void)sink_send_request(now_ms);
            else sink_set_state(PD_SINK_ERROR_RECOVERY, now_ms + PD_SINK_ERROR_RECOVERY_MS);
        }
        break;
    case PD_SINK_REQUESTED:
    case PD_SINK_TRANSITION:
        if (sink_deadline(now_ms)) sink_set_state(PD_SINK_SOFT_RESET, now_ms);
        break;
    case PD_SINK_READY:
        if (g_sink.target_dirty != 0U) sink_begin_selection(now_ms);
        else if ((g_sink.status.epr_active != 0U) && ((int32_t)(now_ms - g_sink.epr_keepalive_ms) >= 0)) {
            if (sink_send_data(USBPD_DATA_EPR_MODE, (const uint32_t[]){ PD_SINK_EPR_MODE_KEEPALIVE << 24 }, 1U, now_ms) == USBPD_OK)
                g_sink.epr_keepalive_ms = now_ms + PD_SINK_EPR_KEEPALIVE_MS;
        }
        break;
    case PD_SINK_SOFT_RESET:
        if ((g_sink.peer_soft_reset != 0U) &&
            (usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_ACCEPT, now_ms) == USBPD_OK)) {
            g_sink.peer_soft_reset = 0U;
            g_sink.get_source_cap_sent = 0U;
            g_sink.source_valid = 0U;
            sink_set_state(PD_SINK_DISCOVERY, now_ms + PD_SINK_WAIT_CAP_MS);
        } else if ((g_sink.peer_soft_reset == 0U) &&
                   (usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_SOFT_RESET, now_ms) == USBPD_OK)) {
            g_sink.get_source_cap_sent = 0U;
            g_sink.source_valid = 0U;
            sink_set_state(PD_SINK_DISCOVERY, now_ms + PD_SINK_WAIT_CAP_MS);
        }
        break;
    case PD_SINK_HARD_RESET:
        if (usbpd_protocol_send_hard_reset(now_ms) == USBPD_OK) sink_set_state(PD_SINK_ERROR_RECOVERY, now_ms + PD_SINK_ERROR_RECOVERY_MS);
        break;
    case PD_SINK_ERROR_RECOVERY:
        if (sink_deadline(now_ms)) {
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
    if (g_sink.config.features.trace_print != 0U) pd_trace_service(1U);
}

void pd_sink_trace_service(uint8_t max_records) { pd_trace_service(max_records); }
void pd_sink_trace_clear(void) { pd_trace_reset(); }
