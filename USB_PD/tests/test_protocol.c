#include "usbpd_phy_ch32x035.h"
#include "usbpd_protocol.h"
#include <assert.h>
#include <string.h>

static uint8_t g_tx[USBPD_MAX_FRAME_LEN];
static uint8_t g_tx_len;
static struct usbpd_frame_t g_rx;
static uint8_t g_rx_ready;
static int g_rx_result;
static uint8_t g_goodcrc_events;
static uint8_t g_timeout_events;
static uint8_t g_protocol_errors;
static uint8_t g_tx_count;

int usbpd_phy_init(void) { return USBPD_OK; }
void usbpd_phy_task(uint32_t now_ms) { (void)now_ms; }
void usbpd_phy_set_cc(uint8_t cc) { (void)cc; }
void usbpd_phy_set_roles(uint8_t power_role, uint8_t data_role)
{
    (void)power_role;
    (void)data_role;
}
void usbpd_phy_get_cc(enum usbpd_cc_e *cc1, enum usbpd_cc_e *cc2) { (void)cc1; (void)cc2; }
int usbpd_phy_get_vbus(uint32_t *mv) { (void)mv; return USBPD_BUSY; }
int usbpd_phy_send(uint8_t sop, const uint8_t *raw, uint8_t len, uint32_t now_ms)
{
    (void)sop;
    (void)now_ms;
    memcpy(g_tx, raw, len);
    g_tx_len = len;
    g_tx_count++;
    return USBPD_OK;
}
int usbpd_phy_receive(struct usbpd_frame_t *frame)
{
    int result;

    if (g_rx_ready == 0U) return USBPD_BUSY;
    result = g_rx_result;
    g_rx_result = USBPD_OK;
    g_rx_ready = 0U;
    if (result != USBPD_OK) return result;
    *frame = g_rx;
    return USBPD_OK;
}
static void event(uint8_t type, const struct usbpd_protocol_msg_t *msg, void *arg)
{
    (void)msg;
    (void)arg;
    if (type == USBPD_PROTOCOL_TX_GOODCRC) g_goodcrc_events++;
    if (type == USBPD_PROTOCOL_TX_TIMEOUT) g_timeout_events++;
    if (type == USBPD_PROTOCOL_ERROR) g_protocol_errors++;
}

static void queue_goodcrc(uint8_t id)
{
    memset(&g_rx, 0, sizeof(g_rx));
    g_rx.sop = USBPD_SOP;
    g_rx.header.bits.type = USBPD_CTRL_GOODCRC;
    g_rx.header.bits.message_id = id;
    g_rx_ready = 1U;
}

uint8_t usbpd_phy_tx_idle(void) { return 1U; }
uint8_t usbpd_phy_take_rx_overflow(void) { return 0U; }
void usbpd_phy_reset_rx(void) { }

int main(void)
{
    union usbpd_header_u header;

    usbpd_protocol_init(event, 0);
    assert(usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_GET_SOURCE_CAP, 10U) == USBPD_OK);
    assert(g_tx_len == 2U);
    header.bytes[0] = g_tx[0];
    header.bytes[1] = g_tx[1];
    assert(header.bits.type == USBPD_CTRL_GET_SOURCE_CAP);
    assert(header.bits.message_id == 0U);

    /* MessageID does not advance until GoodCRC; the frame is retried twice. */
    usbpd_protocol_task(12U);
    assert(g_tx_count == 2U);
    header.bytes[0] = g_tx[0];
    header.bytes[1] = g_tx[1];
    assert(header.bits.message_id == 0U);
    queue_goodcrc(0U);
    usbpd_protocol_task(13U);
    assert(g_goodcrc_events == 1U);

    assert(usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_GET_STATUS, 14U) == USBPD_OK);
    header.bytes[0] = g_tx[0];
    header.bytes[1] = g_tx[1];
    assert(header.bits.message_id == 1U);
    queue_goodcrc(1U);
    usbpd_protocol_task(15U);

    /* A valid Request Chunk has Data Size zero and requests chunk number one. */
    {
        uint8_t data[30];
        union usbpd_ext_header_u ext;
        memset(data, 0xA5, sizeof(data));
        assert(usbpd_protocol_send_extended(USBPD_EXT_SECURITY_REQUEST,
                                            data, sizeof(data), 20U) == USBPD_OK);
        header.bytes[0] = g_tx[0];
        header.bytes[1] = g_tx[1];
        assert(header.bits.extended == 1U);
        assert(header.bits.data_objects == 7U);
        queue_goodcrc(2U);
        usbpd_protocol_task(21U);

        memset(&g_rx, 0, sizeof(g_rx));
        g_rx.sop = USBPD_SOP;
        g_rx.header.bits.type = USBPD_EXT_SECURITY_REQUEST;
        g_rx.header.bits.extended = 1U;
        g_rx.header.bits.data_objects = 1U;
        g_rx.header.bits.message_id = 0U;
        g_rx.payload_len = 1U;
        ext.raw = 0U;
        ext.bits.chunked = 1U;
        ext.bits.request_chunk = 1U;
        ext.bits.chunk_number = 1U;
        g_rx.payload[0] = ext.bytes[0];
        g_rx.payload[1] = ext.bytes[1];
        g_rx_ready = 1U;
        usbpd_protocol_task(22U);
        header.bytes[0] = g_tx[0];
        header.bytes[1] = g_tx[1];
        ext.bytes[0] = g_tx[2];
        ext.bytes[1] = g_tx[3];
        assert(header.bits.message_id == 3U);
        assert(ext.bits.chunk_number == 1U);
        assert(ext.bits.data_size == sizeof(data));
    }
    assert(g_timeout_events == 0U);

    usbpd_protocol_reset();
    {
        uint8_t before = g_tx_count;
        assert(usbpd_protocol_send_ctrl(USBPD_SOP, USBPD_CTRL_GET_STATUS, 50U) == USBPD_OK);
        usbpd_protocol_task(52U);
        usbpd_protocol_task(54U);
        usbpd_protocol_task(56U);
        assert(g_tx_count == (uint8_t)(before + 3U));
        assert(g_timeout_events == 1U);
    }
    assert(g_protocol_errors == 0U);

    /* A malformed PHY slot is consumed, reported, and does not stall polling. */
    g_rx_result = USBPD_ERR_PROTOCOL;
    g_rx_ready = 1U;
    usbpd_protocol_task(60U);
    assert(g_protocol_errors == 1U);
    usbpd_protocol_task(61U);
    assert(g_protocol_errors == 1U);
    return 0;
}
