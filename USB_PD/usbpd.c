#include "usbpd.h"
#include "usbpd_phy_ch32x035.h"
#include "usbpd_protocol.h"
#include <string.h>

struct usbpd_port_context_t
{
    usbpd_config_t config;
    uint32_t toggle_deadline_ms;
    uint8_t active_role;
    uint8_t drp_phase;
    uint8_t policy_was_attached;
    uint8_t initialized;
};

static struct usbpd_port_context_t g_port;

static uint8_t usbpd_valid_role(uint8_t role)
{
    return (role == USBPD_POWER_ROLE_SINK) || (role == USBPD_POWER_ROLE_SOURCE);
}

static uint16_t usbpd_drp_period(void)
{
    return (g_port.config.drp_toggle_ms != 0U) ? g_port.config.drp_toggle_ms : USBPD_DRP_TOGGLE_DEFAULT_MS;
}

static void usbpd_apply_drp_phase(void)
{
    if (g_port.drp_phase == USBPD_POWER_ROLE_SOURCE)
        usbpd_phy_set_rp(g_port.config.source.source_pdo[0].fixed.current_10ma * 10U);
    usbpd_phy_set_pull((g_port.drp_phase == USBPD_POWER_ROLE_SINK) ? USBPD_CC_PULL_RD : USBPD_CC_PULL_RP);
}

static void usbpd_restart_drp(uint32_t now_ms)
{
    usbpd_protocol_reset();
    g_port.active_role = USBPD_ROLE_INACTIVE;
    g_port.policy_was_attached = 0U;
    g_port.drp_phase = g_port.config.preferred_role;
    usbpd_apply_drp_phase();
    g_port.toggle_deadline_ms = now_ms + usbpd_drp_period();
}

static uint8_t usbpd_cc_has_rp(enum usbpd_cc_e cc)
{
    return (cc == USBPD_CC_RP_DEF) || (cc == USBPD_CC_RP_1A5) || (cc == USBPD_CC_RP_3A);
}

static void usbpd_configure_drp_responses(void)
{
    struct pd_source_config_t *source = &g_port.config.source;
    const struct pd_sink_config_t *sink = &g_port.config.sink;
    uint8_t i;

    source->features.drp = 1U;
    source->source_pdo[0].fixed.drp = 1U;
    if (source->sink_pdo_count == 0U)
    {
        source->sink_pdo_count = sink->sink_pdo_count;
        for (i = 0U; i < sink->sink_pdo_count; ++i)
            source->sink_pdo[i] = sink->sink_pdo[i];
    }
    if ((source->sink_pdo_count != 0U) && (source->sink_pdo[0].common.type == USBPD_PDO_FIXED))
        source->sink_pdo[0].sink_fixed.drp = 1U;
    if ((source->sink_cap_ext_valid == 0U) && (sink->sink_cap_ext_valid != 0U))
    {
        source->sink_cap_ext = sink->sink_cap_ext;
        source->sink_cap_ext_valid = 1U;
    }
}

static void usbpd_drp_discovery(uint32_t now_ms)
{
    enum usbpd_cc_e cc1 = USBPD_CC_OPEN;
    enum usbpd_cc_e cc2 = USBPD_CC_OPEN;

    usbpd_phy_task(now_ms);
    usbpd_phy_get_cc(&cc1, &cc2);
    if ((g_port.drp_phase == USBPD_POWER_ROLE_SINK) && (usbpd_cc_has_rp(cc1) || usbpd_cc_has_rp(cc2)))
    {
        if (pd_sink_policy_init(&g_port.config.sink, 0U) == USBPD_OK)
        {
            g_port.active_role = USBPD_POWER_ROLE_SINK;
            pd_sink_task(now_ms);
        }
        return;
    }
    if ((g_port.drp_phase == USBPD_POWER_ROLE_SOURCE) && ((cc1 == USBPD_CC_RD) || (cc2 == USBPD_CC_RD)))
    {
        if (pd_source_policy_init(&g_port.config.source, 0U) == USBPD_OK)
        {
            g_port.active_role = USBPD_POWER_ROLE_SOURCE;
            pd_source_task(now_ms);
        }
        return;
    }
    if ((int32_t)(now_ms - g_port.toggle_deadline_ms) >= 0)
    {
        g_port.drp_phase =
            (g_port.drp_phase == USBPD_POWER_ROLE_SINK) ? USBPD_POWER_ROLE_SOURCE : USBPD_POWER_ROLE_SINK;
        usbpd_apply_drp_phase();
        g_port.toggle_deadline_ms = now_ms + usbpd_drp_period();
    }
}

int usbpd_request(uint8_t port, uint32_t voltage_mv, uint32_t current_ma)
{
    if (port != 0U)
        return USBPD_ERR_UNSUPPORTED;
    if ((g_port.initialized == 0U) || (g_port.active_role != USBPD_POWER_ROLE_SINK))
        return USBPD_ERR_STATE;
    return pd_sink_request(voltage_mv, current_ma);
}

int usbpd_get_status(uint8_t port, usbpd_status_t *status)
{
    if (port != 0U)
        return USBPD_ERR_UNSUPPORTED;
    if (status == 0)
        return USBPD_ERR_PARAM;
    if (g_port.initialized == 0U)
        return USBPD_ERR_STATE;

    memset(status, 0, sizeof(*status));
    status->port_type = g_port.config.port_type;
    status->power_role = g_port.active_role;
    if (g_port.active_role == USBPD_POWER_ROLE_SINK)
    {
        (void)pd_sink_get_status(&status->policy.sink);
        status->attached = pd_sink_is_attached();
    }
    else if (g_port.active_role == USBPD_POWER_ROLE_SOURCE)
    {
        (void)pd_source_get_status(&status->policy.source);
        status->attached = pd_source_is_attached();
    }
    return USBPD_OK;
}

int usbpd_send_control(uint8_t port, uint8_t sop, uint8_t type)
{
    if (port != 0U)
        return USBPD_ERR_UNSUPPORTED;
    if (g_port.active_role == USBPD_POWER_ROLE_SINK)
        return pd_sink_send_control(sop, type);
    if (g_port.active_role == USBPD_POWER_ROLE_SOURCE)
        return pd_source_send_control(sop, type);
    return USBPD_ERR_STATE;
}

int usbpd_send_data_objects(uint8_t port, uint8_t sop, uint8_t type, const uint32_t *objects, uint8_t count)
{
    if (port != 0U)
        return USBPD_ERR_UNSUPPORTED;
    if (g_port.active_role == USBPD_POWER_ROLE_SINK)
        return pd_sink_send_data_objects(sop, type, objects, count);
    if (g_port.active_role == USBPD_POWER_ROLE_SOURCE)
        return pd_source_send_data_objects(sop, type, objects, count);
    return USBPD_ERR_STATE;
}

int usbpd_send_extended(uint8_t port, uint8_t sop, uint8_t type, const uint8_t *data, uint16_t length)
{
    if (port != 0U)
        return USBPD_ERR_UNSUPPORTED;
    if (g_port.active_role == USBPD_POWER_ROLE_SINK)
        return pd_sink_send_extended(sop, type, data, length);
    if (g_port.active_role == USBPD_POWER_ROLE_SOURCE)
        return pd_source_send_extended(sop, type, data, length);
    return USBPD_ERR_STATE;
}

int usbpd_init(uint8_t port, const usbpd_config_t *config)
{
    int ret;
    if (port != 0U)
        return USBPD_ERR_UNSUPPORTED;
    if ((config == 0) || (config->port_type > USBPD_PORT_DRP))
        return USBPD_ERR_PARAM;

    memset(&g_port, 0, sizeof(g_port));
    g_port.config = *config;
    g_port.active_role = USBPD_ROLE_INACTIVE;

    if (config->port_type == USBPD_PORT_SINK)
    {
        ret = pd_sink_init(&config->sink);
        if (ret == USBPD_OK)
            g_port.active_role = USBPD_POWER_ROLE_SINK;
    }
    else if (config->port_type == USBPD_PORT_SOURCE)
    {
        ret = pd_source_init(&config->source);
        if (ret == USBPD_OK)
            g_port.active_role = USBPD_POWER_ROLE_SOURCE;
    }
    else
    {
        if (usbpd_valid_role(config->preferred_role) == 0U)
            return USBPD_ERR_PARAM;
        usbpd_configure_drp_responses();
        usbpd_observer_init();
        ret = usbpd_phy_init();
        if (ret == USBPD_OK)
            ret = pd_sink_policy_init(&g_port.config.sink, 0U);
        if (ret == USBPD_OK)
            ret = pd_source_policy_init(&g_port.config.source, 0U);
        if (ret == USBPD_OK)
            usbpd_restart_drp(0U);
    }
    if (ret == USBPD_OK)
        g_port.initialized = 1U;
    return ret;
}

void usbpd_task(uint8_t port, uint32_t now_ms)
{
    uint8_t attached;
    if ((port != 0U) || (g_port.initialized == 0U))
        return;

    if ((g_port.config.port_type == USBPD_PORT_DRP) && (g_port.active_role == USBPD_ROLE_INACTIVE))
    {
        usbpd_drp_discovery(now_ms);
        return;
    }

    if (g_port.active_role == USBPD_POWER_ROLE_SINK)
    {
        pd_sink_task(now_ms);
        attached = pd_sink_is_attached();
    }
    else if (g_port.active_role == USBPD_POWER_ROLE_SOURCE)
    {
        pd_source_task(now_ms);
        attached = pd_source_is_attached();
    }
    else
    {
        return;
    }

    if (g_port.config.port_type != USBPD_PORT_DRP)
        return;
    if (attached != 0U)
        g_port.policy_was_attached = 1U;
    else if (g_port.policy_was_attached != 0U)
        usbpd_restart_drp(now_ms);
}
