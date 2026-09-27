#include "pd_sink.h"
#include "usbpd_phy_ch32x035.h"
#include <assert.h>
#include <string.h>

static struct usbpd_frame_t g_rx;
static uint8_t g_rx_ready;
static uint8_t g_tx[USBPD_MAX_FRAME_LEN];
static uint8_t g_tx_len;

int usbpd_phy_init(void) { return USBPD_OK; }
void usbpd_phy_task(uint32_t now_ms) { (void)now_ms; }
void usbpd_phy_set_cc(uint8_t cc) { assert((cc == 1U) || (cc == 2U)); }
void usbpd_phy_set_pull(uint8_t pull) { assert(pull == USBPD_CC_PULL_RD); }
void usbpd_phy_set_roles(uint8_t power_role, uint8_t data_role)
{
    assert(power_role == USBPD_POWER_ROLE_SINK);
    assert(data_role == USBPD_DATA_ROLE_UFP);
}
void usbpd_phy_get_cc(enum usbpd_cc_e *cc1, enum usbpd_cc_e *cc2)
{
    *cc1 = USBPD_CC_RP_3A;
    *cc2 = USBPD_CC_OPEN;
}
int usbpd_phy_get_vbus(uint32_t *mv) { *mv = 5000U; return USBPD_OK; }
int usbpd_phy_send(uint8_t sop, const uint8_t *raw, uint8_t len, uint32_t now_ms)
{
    (void)sop;
    (void)now_ms;
    assert(len <= sizeof(g_tx));
    if (len != 0U) memcpy(g_tx, raw, len);
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

static void queue_data(uint8_t type, uint8_t id, const uint32_t *objects,
                       uint8_t count)
{
    memset(&g_rx, 0, sizeof(g_rx));
    g_rx.sop = USBPD_SOP;
    g_rx.header.bits.type = type;
    g_rx.header.bits.revision = USBPD_REV30;
    g_rx.header.bits.message_id = id;
    g_rx.header.bits.data_objects = count;
    g_rx.payload_len = count;
    memcpy(g_rx.payload, objects, count * 4U);
    g_rx_ready = 1U;
}

static void queue_ext_chunk(uint8_t id, uint8_t chunk, const uint8_t *data,
                            uint8_t bytes, uint16_t total)
{
    union usbpd_ext_header_u ext;
    uint8_t objects = (uint8_t)((bytes + 2U + 3U) / 4U);
    memset(&g_rx, 0, sizeof(g_rx));
    g_rx.sop = USBPD_SOP;
    g_rx.header.bits.type = USBPD_EXT_EPR_SOURCE_CAP;
    g_rx.header.bits.revision = USBPD_REV30;
    g_rx.header.bits.message_id = id;
    g_rx.header.bits.data_objects = objects;
    g_rx.header.bits.extended = 1U;
    g_rx.payload_len = objects;
    ext.raw = 0U;
    ext.bits.data_size = total;
    ext.bits.chunked = 1U;
    ext.bits.chunk_number = chunk;
    g_rx.payload[0] = ext.bytes[0];
    g_rx.payload[1] = ext.bytes[1];
    memcpy(&g_rx.payload[2], data, bytes);
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
    struct pd_sink_config_t config;
    struct pd_sink_status_t status;
    union usbpd_pdo_u source;
    union usbpd_pdo_u epr_caps[8];
    union usbpd_epr_mode_do_u mode;
    union usbpd_header_u header;
    union usbpd_rdo_u rdo;

    memset(&config, 0, sizeof(config));
    config.max_voltage_mv = 28000U;
    config.max_current_ma = 5000U;
    config.max_power_mw = 140000U;
    config.features.epr = 1U;
    config.sink_pdo_count = 1U;
    config.sink_pdo[0].sink_fixed.type = USBPD_PDO_FIXED;
    config.sink_pdo[0].sink_fixed.voltage_50mv = 100U;
    config.sink_pdo[0].sink_fixed.current_10ma = 300U;
    config.epr_sink_pdo_count = 8U;
    config.epr_sink_pdo[0] = config.sink_pdo[0];
    config.epr_sink_pdo[7].sink_avs.type = USBPD_PDO_APDO;
    config.epr_sink_pdo[7].sink_avs.subtype = USBPD_APDO_EPR_AVS;
    config.epr_sink_pdo[7].sink_avs.min_voltage_100mv = 150U;
    config.epr_sink_pdo[7].sink_avs.max_voltage_100mv = 280U;
    config.epr_sink_pdo[7].sink_avs.pdp_w = 140U;

    assert(pd_sink_init(&config) == USBPD_OK);
    assert(pd_sink_request(28000U, 3000U) == USBPD_OK);
    pd_sink_task(0U);
    pd_sink_task(100U);

    source.raw = 0U;
    source.fixed.type = USBPD_PDO_FIXED;
    source.fixed.voltage_50mv = 100U;
    source.fixed.current_10ma = 300U;
    source.fixed.epr_capable = 1U;
    queue_data(USBPD_DATA_SOURCE_CAP, 0U, &source.raw, 1U);
    pd_sink_task(101U);
    header = tx_header();
    assert(header.bits.type == USBPD_DATA_REQUEST);

    queue_goodcrc(0U);
    pd_sink_task(102U);
    queue_control(USBPD_CTRL_ACCEPT, 1U);
    pd_sink_task(103U);
    queue_control(USBPD_CTRL_PS_RDY, 2U);
    pd_sink_task(104U);
    pd_sink_task(105U);
    header = tx_header();
    assert(header.bits.type == USBPD_DATA_EPR_MODE);
    memcpy(&mode.raw, &g_tx[2], sizeof(mode.raw));
    assert(mode.bits.action == USBPD_EPR_MODE_ENTER);

    queue_goodcrc(1U);
    pd_sink_task(106U);
    mode.raw = 0U;
    mode.bits.action = USBPD_EPR_MODE_ENTER_ACK;
    queue_data(USBPD_DATA_EPR_MODE, 3U, &mode.raw, 1U);
    pd_sink_task(107U);
    mode.bits.action = USBPD_EPR_MODE_ENTER_SUCCEEDED;
    queue_data(USBPD_DATA_EPR_MODE, 4U, &mode.raw, 1U);
    pd_sink_task(108U);

    memset(epr_caps, 0, sizeof(epr_caps));
    epr_caps[0] = source;
    epr_caps[7].avs.type = USBPD_PDO_APDO;
    epr_caps[7].avs.subtype = USBPD_APDO_EPR_AVS;
    epr_caps[7].avs.min_voltage_100mv = 150U;
    epr_caps[7].avs.max_voltage_100mv = 280U;
    epr_caps[7].avs.pdp_w = 140U;
    queue_ext_chunk(5U, 0U, (const uint8_t *)epr_caps, 26U, sizeof(epr_caps));
    pd_sink_task(109U);
    header = tx_header();
    assert(header.bits.extended == 1U);
    assert(header.bits.type == USBPD_EXT_EPR_SOURCE_CAP);

    queue_goodcrc(2U);
    pd_sink_task(110U);
    queue_ext_chunk(6U, 1U, &((const uint8_t *)epr_caps)[26], 6U,
                    sizeof(epr_caps));
    pd_sink_task(111U);
    header = tx_header();
    assert(header.bits.type == USBPD_DATA_EPR_REQUEST);
    memcpy(rdo.bytes, &g_tx[2], sizeof(rdo.bytes));
    assert(rdo.avs.object_position == 8U);
    assert(rdo.avs.output_voltage_25mv == 1120U);

    queue_goodcrc(3U);
    pd_sink_task(112U);
    queue_control(USBPD_CTRL_PR_SWAP, 7U);
    pd_sink_task(113U);
    header = tx_header();
    assert(header.bits.type == USBPD_CTRL_REJECT);
    queue_goodcrc(4U);
    pd_sink_task(114U);
    queue_control(USBPD_CTRL_DR_SWAP, 8U);
    pd_sink_task(115U);
    header = tx_header();
    assert(header.bits.type == USBPD_CTRL_REJECT);
    queue_goodcrc(5U);
    pd_sink_task(116U);
    queue_control(USBPD_CTRL_VCONN_SWAP, 9U);
    pd_sink_task(117U);
    header = tx_header();
    assert(header.bits.type == USBPD_CTRL_REJECT);

    assert(pd_sink_get_status(&status) == USBPD_OK);
    assert(status.epr_active != 0U);
    return 0;
}
