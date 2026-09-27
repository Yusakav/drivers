#include "usbpd.h"
#include "usbpd_phy_ch32x035.h"
#include <assert.h>
#include <string.h>

static enum usbpd_cc_e g_cc1 = USBPD_CC_OPEN;
static struct usbpd_frame_t g_rx;
static uint8_t g_rx_ready;
static uint8_t g_tx[USBPD_MAX_FRAME_LEN];
static uint8_t g_tx_len;
static uint8_t g_pull;
static uint8_t g_phy_init_count;

int usbpd_phy_init(void)
{
    g_phy_init_count++;
    return USBPD_OK;
}
void usbpd_phy_task(uint32_t now_ms) { (void)now_ms; }
void usbpd_phy_set_cc(uint8_t cc) { assert((cc == 1U) || (cc == 2U)); }
void usbpd_phy_set_pull(uint8_t pull) { g_pull = pull; }
void usbpd_phy_set_rp(uint32_t current_ma) { assert(current_ma == 3000U); }
void usbpd_phy_set_roles(uint8_t power_role, uint8_t data_role)
{
    assert((power_role == USBPD_POWER_ROLE_SINK) ||
           (power_role == USBPD_POWER_ROLE_SOURCE));
    assert((data_role == USBPD_DATA_ROLE_UFP) ||
           (data_role == USBPD_DATA_ROLE_DFP));
}
void usbpd_phy_get_cc(enum usbpd_cc_e *cc1, enum usbpd_cc_e *cc2)
{
    *cc1 = g_cc1;
    *cc2 = USBPD_CC_OPEN;
}
int usbpd_phy_get_vbus(uint32_t *mv) { *mv = 5000U; return USBPD_OK; }
int usbpd_phy_send(uint8_t sop, const uint8_t *raw, uint8_t len,
                   uint32_t now_ms)
{
    (void)sop;
    (void)now_ms;
    if ((raw != 0) && (len != 0U)) memcpy(g_tx, raw, len);
    g_tx_len = len;
    return USBPD_OK;
}
int usbpd_phy_receive(struct usbpd_frame_t *frame)
{
    if (g_rx_ready == 0U) return USBPD_BUSY;
    *frame = g_rx;
    g_rx_ready = 0U;
    return USBPD_OK;
}
uint8_t usbpd_phy_tx_idle(void) { return 1U; }
uint8_t usbpd_phy_take_rx_overflow(void) { return 0U; }
void usbpd_phy_reset_rx(void) { }

static int set_source(uint8_t port, uint8_t enable, uint32_t voltage_mv,
                      uint32_t current_ma, void *context)
{
    (void)port;
    (void)enable;
    (void)voltage_mv;
    (void)current_ma;
    (void)context;
    return USBPD_OK;
}

static void queue_control(uint8_t type, uint8_t id)
{
    memset(&g_rx, 0, sizeof(g_rx));
    g_rx.sop = USBPD_SOP;
    g_rx.header.bits.type = type;
    g_rx.header.bits.revision = USBPD_REV30;
    g_rx.header.bits.message_id = id;
    g_rx_ready = 1U;
}

static union usbpd_header_u tx_header(void)
{
    union usbpd_header_u header;
    assert(g_tx_len >= 2U);
    header.bytes[0] = g_tx[0];
    header.bytes[1] = g_tx[1];
    return header;
}

int main(void)
{
    usbpd_config_t config;
    usbpd_status_t status;
    union usbpd_header_u header;

    memset(&config, 0, sizeof(config));
    config.port_type = USBPD_PORT_DRP;
    config.preferred_role = USBPD_POWER_ROLE_SINK;
    config.drp_toggle_ms = 75U;
    config.sink.max_voltage_mv = 5000U;
    config.sink.max_current_ma = 1000U;
    config.sink.max_power_mw = 5000U;
    config.sink.sink_pdo_count = 1U;
    config.sink.sink_pdo[0].sink_fixed.type = USBPD_PDO_FIXED;
    config.sink.sink_pdo[0].sink_fixed.voltage_50mv = 100U;
    config.sink.sink_pdo[0].sink_fixed.current_10ma = 100U;
    config.source.source_pdo_count = 1U;
    config.source.source_pdo[0].fixed.type = USBPD_PDO_FIXED;
    config.source.source_pdo[0].fixed.voltage_50mv = 100U;
    config.source.source_pdo[0].fixed.current_10ma = 300U;
    config.source.dpm.set_source = set_source;

    assert(usbpd_init(1U, &config) == USBPD_ERR_UNSUPPORTED);
    assert(usbpd_init(0U, &config) == USBPD_OK);
    assert(g_phy_init_count == 1U);
    assert(g_pull == USBPD_CC_PULL_RD);
    assert(usbpd_get_status(0U, &status) == USBPD_OK);
    assert(status.power_role == USBPD_ROLE_INACTIVE);
    assert(usbpd_request(0U, 5000U, 1000U) == USBPD_ERR_STATE);

    usbpd_task(0U, 75U);
    assert(g_pull == USBPD_CC_PULL_RP);
    g_cc1 = USBPD_CC_RD;
    usbpd_task(0U, 76U);
    assert(usbpd_get_status(0U, &status) == USBPD_OK);
    assert(status.power_role == USBPD_POWER_ROLE_SOURCE);
    assert(status.attached != 0U);
    usbpd_task(0U, 77U);
    queue_control(USBPD_CTRL_GET_SINK_CAP, 0U);
    usbpd_task(0U, 78U);
    header = tx_header();
    assert(header.bits.type == USBPD_DATA_SINK_CAP);

    g_cc1 = USBPD_CC_OPEN;
    usbpd_task(0U, 79U);
    assert(usbpd_get_status(0U, &status) == USBPD_OK);
    assert(status.power_role == USBPD_ROLE_INACTIVE);
    assert(g_pull == USBPD_CC_PULL_RD);

    g_cc1 = USBPD_CC_RP_3A;
    usbpd_task(0U, 80U);
    assert(usbpd_get_status(0U, &status) == USBPD_OK);
    assert(status.power_role == USBPD_POWER_ROLE_SINK);
    assert(status.attached != 0U);
    assert(usbpd_request(0U, 5000U, 1000U) == USBPD_OK);
    return 0;
}
