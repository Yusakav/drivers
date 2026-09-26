#include "usbpd_device.h"
#include "pd_trace.h"
#include "ch32x035_usbpd.h"
#include "usb_vbus_measure.h"
#include "debug.h"
#include <string.h>

#define USBPD_RX_DEPTH 4U
#define USBPD_TX_TIMEOUT_MS 5U

struct usbpd_device_rx_t {
    uint8_t raw[USBPD_TRACE_FRAME_LEN];
    uint8_t length;
    uint8_t sop;
    uint32_t timestamp_ms;
};

struct usbpd_device_t {
    struct usbpd_device_rx_t rx[USBPD_RX_DEPTH];
    uint8_t tx_dma[USBPD_MAX_FRAME_LEN] __attribute__((aligned(4)));
    uint8_t rx_dma[USBPD_TRACE_FRAME_LEN] __attribute__((aligned(4)));
    uint8_t goodcrc_dma[2] __attribute__((aligned(4)));
    struct {
        uint8_t raw[2];
        uint8_t sop;
        uint32_t timestamp_ms;
    } goodcrc_trace[USBPD_RX_DEPTH];
    volatile uint8_t rx_read;
    volatile uint8_t rx_write;
    volatile uint8_t rx_count;
    volatile uint8_t goodcrc_read;
    volatile uint8_t goodcrc_write;
    volatile uint8_t goodcrc_count;
    volatile uint8_t tx_busy;
    volatile uint8_t selected_cc : 1;
    volatile uint8_t rx_overflow : 1;
    volatile uint8_t reserved : 6;
    uint32_t now_ms;
    uint32_t tx_deadline_ms;
};

static struct usbpd_device_t g_dev;

static uint8_t device_tx_sel(uint8_t sop)
{
    switch (sop) {
    case USBPD_SOP: return UPD_SOP0;
    case USBPD_SOP_PRIME: return UPD_SOP1;
    case USBPD_SOP_DPRIME: return UPD_SOP2;
    case USBPD_SOP_HARD_RESET: return UPD_HARD_RESET;
    case USBPD_SOP_CABLE_RESET: return UPD_CABLE_RESET;
    default: return 0U;
    }
}

static uint8_t device_sop_from_status(uint16_t status, uint8_t length)
{
    switch (status & MASK_PD_STAT) {
    case PD_RX_SOP0: return USBPD_SOP;
    case PD_RX_SOP1_HRST: return (length >= 6U) ? USBPD_SOP_PRIME : USBPD_SOP_HARD_RESET;
    case PD_RX_SOP2_CRST: return (length >= 6U) ? USBPD_SOP_DPRIME : USBPD_SOP_CABLE_RESET;
    default: return USBPD_SOP_INVALID;
    }
}

static void device_enable_rx(void)
{
    USBPD->CONFIG |= PD_ALL_CLR;
    USBPD->CONFIG &= ~PD_ALL_CLR;
    USBPD->CONFIG |= IE_RX_ACT | IE_RX_RESET | PD_DMA_EN;
    USBPD->DMA = (uint32_t)g_dev.rx_dma;
    USBPD->CONTROL &= ~PD_TX_EN;
    USBPD->BMC_CLK_CNT = UPD_TMR_RX_48M;
    USBPD->CONTROL |= BMC_START;
}

static void device_clear_lve(void)
{
    USBPD->PORT_CC1 &= ~CC_LVE;
    USBPD->PORT_CC2 &= ~CC_LVE;
}

static void device_finish_tx(void)
{
    g_dev.tx_busy = 0U;
    device_clear_lve();
    device_enable_rx();
}

static void device_start_tx(uint8_t sop, uint8_t *data, uint8_t length)
{
    USBPD->CONFIG |= IE_TX_END;
    if (g_dev.selected_cc != 0U) {
        USBPD->PORT_CC2 |= CC_LVE;
    } else {
        USBPD->PORT_CC1 |= CC_LVE;
    }
    USBPD->BMC_CLK_CNT = UPD_TMR_TX_48M;
    USBPD->DMA = (uint32_t)data;
    USBPD->TX_SEL = device_tx_sel(sop);
    USBPD->BMC_TX_SZ = length;
    USBPD->CONTROL |= PD_TX_EN;
    USBPD->STATUS &= BMC_AUX_INVALID;
    USBPD->CONTROL |= BMC_START;
}

static void device_queue_rx(uint8_t sop, uint8_t length)
{
    struct usbpd_device_rx_t *slot;
    if (g_dev.rx_count == USBPD_RX_DEPTH) {
        g_dev.rx_read = (uint8_t)((g_dev.rx_read + 1U) % USBPD_RX_DEPTH);
        g_dev.rx_count--;
        g_dev.rx_overflow = 1U;
    }
    slot = &g_dev.rx[g_dev.rx_write];
    slot->length = length;
    slot->sop = sop;
    slot->timestamp_ms = g_dev.now_ms;
    if (length != 0U) {
        memcpy(slot->raw, g_dev.rx_dma, length);
    }
    g_dev.rx_write = (uint8_t)((g_dev.rx_write + 1U) % USBPD_RX_DEPTH);
    g_dev.rx_count++;
}

static void device_queue_goodcrc_trace(uint8_t sop)
{
    uint8_t write = g_dev.goodcrc_write;

    if (g_dev.goodcrc_count == USBPD_RX_DEPTH) {
        g_dev.goodcrc_read = (uint8_t)((g_dev.goodcrc_read + 1U) % USBPD_RX_DEPTH);
        g_dev.goodcrc_count--;
    }

    g_dev.goodcrc_trace[write].raw[0] = g_dev.goodcrc_dma[0];
    g_dev.goodcrc_trace[write].raw[1] = g_dev.goodcrc_dma[1];
    g_dev.goodcrc_trace[write].sop = sop;
    g_dev.goodcrc_trace[write].timestamp_ms = g_dev.now_ms;
    g_dev.goodcrc_write = (uint8_t)((write + 1U) % USBPD_RX_DEPTH);
    g_dev.goodcrc_count++;
}

static void device_send_goodcrc(uint8_t sop, const uint8_t *raw)
{
    union usbpd_header_u rx;
    union usbpd_header_u tx;
    if ((sop != USBPD_SOP) && (sop != USBPD_SOP_PRIME) && (sop != USBPD_SOP_DPRIME)) {
        return;
    }
    rx.bytes[0] = raw[0];
    rx.bytes[1] = raw[1];
    tx.raw = 0U;
    tx.bits.type = USBPD_CTRL_GOODCRC;
    tx.bits.revision = rx.bits.revision;
    tx.bits.message_id = rx.bits.message_id;
    tx.bits.power_role = 0U;
    g_dev.goodcrc_dma[0] = tx.bytes[0];
    g_dev.goodcrc_dma[1] = tx.bytes[1];
    g_dev.tx_busy = 1U;
    g_dev.tx_deadline_ms = g_dev.now_ms + USBPD_TX_TIMEOUT_MS;
    device_queue_goodcrc_trace(sop);
    device_start_tx(sop, g_dev.goodcrc_dma, 2U);
}

static uint8_t device_is_goodcrc(const uint8_t *raw, uint8_t length)
{
    union usbpd_header_u header;

    if ((raw == 0) || (length < 2U)) {
        return 0U;
    }

    header.bytes[0] = raw[0];
    header.bytes[1] = raw[1];
    return ((header.bits.data_objects == 0U) &&
            (header.bits.type == USBPD_CTRL_GOODCRC));
}

static enum usbpd_cc_e device_read_cc(volatile uint16_t *port)
{
    static const uint16_t cmp_reg[] = {
        CC_CMP_22, CC_CMP_45, CC_CMP_55, CC_CMP_66, CC_CMP_95, CC_CMP_123
    };
    static const uint16_t cmp_mv[] = { 22U, 45U, 55U, 66U, 95U, 123U };
    uint16_t voltage = 0U;
    uint8_t i;

    /* PA_CC_AI belongs to the USBPD CC comparator, not GPIOC->INDR. */
    for (i = 0U; i < (uint8_t)(sizeof(cmp_reg) / sizeof(cmp_reg[0])); ++i) {
        *port &= (uint16_t)~(CC_CMP_Mask | PA_CC_AI);
        *port |= cmp_reg[i];
        Delay_Us(2U);
        if ((*port & PA_CC_AI) == 0U) {
            break;
        }
        voltage = cmp_mv[i];
    }

    *port &= (uint16_t)~(CC_CMP_Mask | PA_CC_AI);
    *port |= CC_CMP_66;

    if ((*port & CC_PD) != 0U) {
        if (voltage >= 123U) {
            return USBPD_CC_RP_3A;
        }
        if (voltage >= 66U) {
            return USBPD_CC_RP_1A5;
        }
        if (voltage >= 22U) {
            return USBPD_CC_RP_DEF;
        }
        return USBPD_CC_OPEN;
    }

    return (voltage >= 22U) ? USBPD_CC_RD : USBPD_CC_RA;
}

int usbpd_device_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    memset(&g_dev, 0, sizeof(g_dev));
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBPD, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_14 | GPIO_Pin_15;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &gpio);
    AFIO->CTLR |= USBPD_IN_HVT | USBPD_PHY_V33;
    USBPD->CONFIG = PD_DMA_EN;
    USBPD->STATUS = BUF_ERR | IF_RX_BIT | IF_RX_BYTE | IF_RX_ACT | IF_RX_RESET | IF_TX_END;
    USBPD->PORT_CC1 = CC_CMP_66 | CC_PD;
    USBPD->PORT_CC2 = CC_CMP_66 | CC_PD;
    device_enable_rx();
    NVIC_EnableIRQ(USBPD_IRQn);
    return USBPD_OK;
}

void usbpd_device_set_cc(uint8_t cc)
{
    g_dev.selected_cc = (cc == 2U);
    if (g_dev.selected_cc != 0U) {
        USBPD->CONFIG |= CC_SEL;
    } else {
        USBPD->CONFIG &= ~CC_SEL;
    }
}

void usbpd_device_get_cc(enum usbpd_cc_e *cc1, enum usbpd_cc_e *cc2)
{
    if ((cc1 == 0) || (cc2 == 0)) {
        return;
    }

    *cc1 = device_read_cc(&USBPD->PORT_CC1);
    *cc2 = device_read_cc(&USBPD->PORT_CC2);
}

int usbpd_device_get_vbus(uint32_t *mv)
{
    int value;
    if (mv == 0) return USBPD_ERR;
    value = (int)adc_get_vbus_mv();
    *mv = (value > 0) ? (uint32_t)value : 0U;
    return (*mv >= 800U) ? USBPD_OK : USBPD_BUSY;
}

int usbpd_device_send(uint8_t sop, const uint8_t *raw, uint8_t len, uint32_t now_ms)
{
    if ((sop > USBPD_SOP_CABLE_RESET) || (len > USBPD_MAX_FRAME_LEN) ||
        ((len != 0U) && (raw == 0)) || (g_dev.tx_busy != 0U)) {
        return (g_dev.tx_busy != 0U) ? USBPD_BUSY : USBPD_ERR;
    }
    if (len != 0U) memcpy(g_dev.tx_dma, raw, len);
    g_dev.tx_busy = 1U;
    g_dev.tx_deadline_ms = now_ms + USBPD_TX_TIMEOUT_MS;
    pd_trace_push(PD_TRACE_TX, sop, raw, len, now_ms,
                  (sop >= USBPD_SOP_HARD_RESET) ? PD_TRACE_RESET : 0U);
    device_start_tx(sop, g_dev.tx_dma, len);
    return USBPD_OK;
}

int usbpd_device_receive(struct usbpd_frame_t *frame)
{
    struct usbpd_device_rx_t slot;
    uint8_t payload_len;

    if (frame == 0) return USBPD_ERR;

    __disable_irq();
    if (g_dev.rx_count == 0U) {
        __enable_irq();
        return USBPD_BUSY;
    }
    slot = g_dev.rx[g_dev.rx_read];
    g_dev.rx_read = (uint8_t)((g_dev.rx_read + 1U) % USBPD_RX_DEPTH);
    g_dev.rx_count--;
    __enable_irq();

    memset(frame, 0, sizeof(*frame));
    frame->sop = slot.sop;
    frame->raw_len = slot.length;
    pd_trace_push(PD_TRACE_RX, slot.sop, slot.raw, slot.length,
                  slot.timestamp_ms,
                  (slot.sop >= USBPD_SOP_HARD_RESET) ? PD_TRACE_RESET : 0U);
    if (slot.length >= 2U) {
        frame->header.bytes[0] = slot.raw[0];
        frame->header.bytes[1] = slot.raw[1];
        payload_len = frame->header.bits.data_objects;
        if ((payload_len <= USBPD_MAX_DATA_OBJ) && (slot.length >= (uint8_t)(2U + payload_len * 4U))) {
            frame->payload_len = payload_len;
            if (payload_len != 0U) memcpy(frame->payload, &slot.raw[2], payload_len * 4U);
        } else {
            frame->sop = USBPD_SOP_INVALID;
        }
    }
    return USBPD_OK;
}

uint8_t usbpd_device_tx_idle(void) { return (g_dev.tx_busy == 0U); }

uint8_t usbpd_device_take_rx_overflow(void)
{
    uint8_t overflow;

    __disable_irq();
    overflow = g_dev.rx_overflow;
    g_dev.rx_overflow = 0U;
    __enable_irq();
    return overflow;
}

void usbpd_device_reset_rx(void)
{
    __disable_irq();
    g_dev.rx_read = g_dev.rx_write = g_dev.rx_count = 0U;
    __enable_irq();
}

void usbpd_device_task(uint32_t now_ms)
{
    g_dev.now_ms = now_ms;
    if ((g_dev.tx_busy != 0U) && ((int32_t)(now_ms - g_dev.tx_deadline_ms) >= 0)) {
        USBPD->CONTROL &= (uint16_t)~(PD_TX_EN | BMC_START);
        USBPD->STATUS |= IF_TX_END;
        device_finish_tx();
    }

    for (;;) {
        uint8_t read;
        uint8_t raw[2];
        uint8_t sop;
        uint32_t timestamp_ms;

        __disable_irq();
        if (g_dev.goodcrc_count == 0U) {
            __enable_irq();
            break;
        }

        read = g_dev.goodcrc_read;
        raw[0] = g_dev.goodcrc_trace[read].raw[0];
        raw[1] = g_dev.goodcrc_trace[read].raw[1];
        sop = g_dev.goodcrc_trace[read].sop;
        timestamp_ms = g_dev.goodcrc_trace[read].timestamp_ms;
        g_dev.goodcrc_read = (uint8_t)((read + 1U) % USBPD_RX_DEPTH);
        g_dev.goodcrc_count--;
        __enable_irq();

        pd_trace_push(PD_TRACE_TX, sop, raw, 2U, timestamp_ms,
                      PD_TRACE_GOODCRC);
    }
}

void USBPD_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBPD_IRQHandler(void)
{
    uint16_t status = USBPD->STATUS;
    if ((status & IF_RX_ACT) != 0U) {
        uint8_t length;
        uint8_t sop;
        USBPD->STATUS |= IF_RX_ACT;
        length = (uint8_t)USBPD->BMC_BYTE_CNT;
        if (length > USBPD_TRACE_FRAME_LEN) length = USBPD_TRACE_FRAME_LEN;
        sop = device_sop_from_status(status, length);
        if (sop != USBPD_SOP_INVALID) {
            if ((sop <= USBPD_SOP_DPRIME) && (length >= 6U) &&
                (device_is_goodcrc(g_dev.rx_dma, length) == 0U)) {
                /* Required receiver response timing; this is the only ISR delay. */
                Delay_Us(30U);
                device_send_goodcrc(sop, g_dev.rx_dma);
            }
            device_queue_rx(sop, length);
        }
    }
    if ((status & IF_TX_END) != 0U) {
        USBPD->STATUS |= IF_TX_END;
        device_finish_tx();
    }
    if ((status & IF_RX_RESET) != 0U) {
        USBPD->STATUS |= IF_RX_RESET;
    }
}
