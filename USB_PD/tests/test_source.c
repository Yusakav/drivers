#include "pd_source.h"
#include "usbpd_phy_ch32x035.h"
#include <assert.h>
#include <string.h>

static struct usbpd_frame_t g_rx;
static uint8_t g_rx_ready;
static uint8_t g_tx[USBPD_MAX_FRAME_LEN];
static uint8_t g_tx_len;
static uint8_t g_tx_sop;
static uint8_t g_tx_count;
static uint8_t g_source_enabled;
static uint32_t g_source_mv;
static uint32_t g_source_ma;

static int get_extended(uint8_t port, uint8_t type, uint8_t *data,
                        uint16_t *length, void *context)
{
    (void)context;
    assert(port == 0U);
    if (type == USBPD_EXT_PPS_STATUS) {
        *length = 0U;
        return USBPD_OK;
    }
    if ((type != USBPD_EXT_STATUS) || (*length < 7U))
        return USBPD_ERR_UNSUPPORTED;
    memset(data, 0, 7U);
    *length = 7U;
    return USBPD_OK;
}

int usbpd_phy_init(void) { return USBPD_OK; }
void usbpd_phy_task(uint32_t now_ms) { (void)now_ms; }
void usbpd_phy_set_cc(uint8_t cc) { assert((cc == 1U) || (cc == 2U)); }
void usbpd_phy_set_pull(uint8_t pull) { assert(pull == USBPD_CC_PULL_RP); }
void usbpd_phy_set_rp(uint32_t current_ma) { assert(current_ma == 3000U); }
void usbpd_phy_set_roles(uint8_t power_role, uint8_t data_role)
{
    assert(power_role == USBPD_POWER_ROLE_SOURCE);
    assert(data_role == USBPD_DATA_ROLE_DFP);
}
void usbpd_phy_get_cc(enum usbpd_cc_e *cc1, enum usbpd_cc_e *cc2)
{
    *cc1 = USBPD_CC_RD;
    *cc2 = USBPD_CC_OPEN;
}
int usbpd_phy_get_vbus(uint32_t *mv) { *mv = g_source_mv; return USBPD_OK; }
int usbpd_phy_send(uint8_t sop, const uint8_t *raw, uint8_t len,
                   uint32_t now_ms)
{
    (void)now_ms;
    assert(len <= sizeof(g_tx));
    if (len != 0U) memcpy(g_tx, raw, len);
    g_tx_len = len;
    g_tx_sop = sop;
    g_tx_count++;
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
    (void)context;
    assert(port == 0U);
    g_source_enabled = enable;
    g_source_mv = voltage_mv;
    g_source_ma = current_ma;
    return USBPD_OK;
}

static uint8_t source_ready(uint8_t port, uint32_t voltage_mv, void *context)
{
    (void)context;
    assert(port == 0U);
    return (g_source_enabled != 0U) && (g_source_mv == voltage_mv);
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

static void queue_goodcrc(uint8_t id)
{
    queue_control(USBPD_CTRL_GOODCRC, id);
}

static void queue_request(uint8_t id, union usbpd_rdo_u request)
{
    memset(&g_rx, 0, sizeof(g_rx));
    g_rx.sop = USBPD_SOP;
    g_rx.header.bits.type = USBPD_DATA_REQUEST;
    g_rx.header.bits.revision = USBPD_REV30;
    g_rx.header.bits.message_id = id;
    g_rx.header.bits.data_objects = 1U;
    g_rx.payload_len = 1U;
    memcpy(g_rx.payload, request.bytes, sizeof(request.bytes));
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
    struct pd_source_config_t config;
    struct pd_source_status_t status;
    union usbpd_header_u header;
    union usbpd_rdo_u request;
    uint8_t tx_count;

    memset(&config, 0, sizeof(config));
    config.source_pdo_count = 1U;
    config.source_pdo[0].fixed.type = USBPD_PDO_FIXED;
    config.source_pdo[0].fixed.voltage_50mv = 100U;
    config.source_pdo[0].fixed.current_10ma = 300U;
    config.dpm.set_source = set_source;
    config.dpm.source_ready = source_ready;
    config.dpm.get_extended = get_extended;
    config.source_info_valid = 1U;
    config.source_info.bits.maximum_pdp_w = 15U;
    config.source_info.bits.present_pdp_w = 15U;
    config.source_info.bits.reported_pdp_w = 15U;
    config.sink_pdo_count = 1U;
    config.sink_pdo[0].sink_fixed.type = USBPD_PDO_FIXED;
    config.sink_pdo[0].sink_fixed.voltage_50mv = 100U;
    config.sink_pdo[0].sink_fixed.current_10ma = 100U;

    assert(pd_source_init(&config) == USBPD_OK);
    pd_source_task(0U);
    pd_source_task(100U);
    assert(g_source_enabled != 0U);
    assert(g_source_mv == 5000U);
    assert(g_source_ma == 3000U);
    pd_source_task(101U);
    pd_source_task(102U);
    header = tx_header();
    assert(header.bits.type == USBPD_DATA_SOURCE_CAP);
    assert(header.bits.power_role == USBPD_POWER_ROLE_SOURCE);

    queue_goodcrc(0U);
    pd_source_task(103U);
    request.raw = 0U;
    request.fixed.object_position = 1U;
    request.fixed.operating_current_10ma = 100U;
    request.fixed.max_current_10ma = 100U;
    queue_request(0U, request);
    pd_source_task(104U);
    header = tx_header();
    assert(header.bits.type == USBPD_CTRL_ACCEPT);
    assert(g_source_mv == 5000U);
    assert(g_source_ma == 3000U);

    queue_goodcrc(1U);
    pd_source_task(105U);
    assert(g_source_ma == 1000U);
    pd_source_task(106U);
    header = tx_header();
    assert(header.bits.type == USBPD_CTRL_PS_RDY);
    assert(pd_source_get_status(&status) == USBPD_OK);
    assert(status.contract_valid != 0U);
    assert(status.negotiated_voltage_mv == 5000U);
    assert(status.negotiated_current_ma == 1000U);

    queue_goodcrc(2U);
    pd_source_task(107U);
    request.fixed.object_position = 2U;
    queue_request(1U, request);
    pd_source_task(108U);
    header = tx_header();
    assert(header.bits.type == USBPD_CTRL_REJECT);

    queue_goodcrc(3U);
    pd_source_task(109U);
    queue_control(USBPD_CTRL_GET_SOURCE_INFO, 2U);
    pd_source_task(110U);
    header = tx_header();
    assert(header.bits.type == USBPD_DATA_SOURCE_INFO);
    queue_goodcrc(4U);
    pd_source_task(111U);

    queue_control(USBPD_CTRL_GET_STATUS, 3U);
    pd_source_task(112U);
    header = tx_header();
    assert(header.bits.extended != 0U);
    assert(header.bits.type == USBPD_EXT_STATUS);
    queue_goodcrc(5U);
    pd_source_task(113U);

    queue_control(USBPD_CTRL_GET_PPS_STATUS, 4U);
    pd_source_task(114U);
    header = tx_header();
    assert(header.bits.type == USBPD_CTRL_NOT_SUPPORTED);
    queue_goodcrc(6U);
    pd_source_task(115U);

    queue_control(USBPD_CTRL_GET_SINK_CAP, 5U);
    pd_source_task(116U);
    header = tx_header();
    assert(header.bits.type == USBPD_DATA_SINK_CAP);
    queue_goodcrc(7U);
    pd_source_task(117U);

    queue_control(USBPD_CTRL_PR_SWAP, 6U);
    pd_source_task(118U);
    header = tx_header();
    assert(header.bits.type == USBPD_CTRL_REJECT);
    queue_goodcrc(0U);
    pd_source_task(119U);
    queue_control(USBPD_CTRL_DR_SWAP, 7U);
    pd_source_task(120U);
    header = tx_header();
    assert(header.bits.type == USBPD_CTRL_REJECT);
    queue_goodcrc(1U);
    pd_source_task(121U);
    queue_control(USBPD_CTRL_VCONN_SWAP, 0U);
    pd_source_task(122U);
    header = tx_header();
    assert(header.bits.type == USBPD_CTRL_REJECT);

    tx_count = g_tx_count;
    memset(&g_rx, 0, sizeof(g_rx));
    g_rx.sop = USBPD_SOP_HARD_RESET;
    g_rx_ready = 1U;
    pd_source_task(123U);
    assert(g_tx_count == tx_count);
    assert(g_tx_sop != USBPD_SOP_HARD_RESET);
    assert(g_source_enabled == 0U);
    return 0;
}
