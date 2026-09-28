/**
* @file usbpd_phy_ch32x035.c
* @brief USB PD PHY 层（CH32X035 硬件）实现
*
* 所属模块：USB PD 协议栈（PHY/硬件抽象层，绑定 CH32X035 MCU 内置 USBPD 控制器）。
* 对应头文件：usbpd_phy_ch32x035.h；依赖：ch32x035_usbpd.h / usb_vbus_measure.h / usbpd_frame.h。
*
* 本文件实现：
*   - CC 线 Rp/Rd 检测（内部比较器逐档切换）、Rp 电流档位设置、VCONN/LVE 开关；
*   - BMC 编码/解码、SOP0/1/2/HardReset/CableReset 发送与接收；
*   - DMA 收发缓冲（tx_dma / rx_dma / goodcrc_dma）与双环形队列（rx / goodcrc_trace）；
*   - 中断处理 USBPD_IRQHandler：RX_ACT 入队、自动 GoodCRC 应答、TX_END 完成；
*   - TX 超时强停、GoodCRC trace 刷写；
*   - VBUS ADC 采集。
*
* 注意：g_dev.rx_overflow / rx_read / rx_write / rx_count 等在中断与主循环间共享，
* 读写均用 __disable_irq() / __enable_irq() 关中断保护。
*/
#include "usbpd_phy_ch32x035.h"
#include "usbpd_frame.h"
#include "usbpd_message.h"
#include "ch32x035_usbpd.h"
#include "usb_vbus_measure.h"
#include "debug.h"
#include <string.h>

/* ===== PHY 层私有参数 ===== */
#define USBPD_RX_DEPTH 4U       /* 接收环形缓冲深度（帧数量） */
#define USBPD_TX_TIMEOUT_MS 5U  /* BMC 发送超时（毫秒）——超时后强停并复位 tx_busy */

/* ===== 内部类型 ===== */
/** @brief 单条接收帧缓冲（从 DMA 拷贝而来，供协议层轮询消费） */
struct usbpd_phy_rx_t
{
    uint8_t raw[USBPD_TRACE_FRAME_LEN];  /* 原始线帧字节（含头、DO、CRC） */
    uint8_t length;                       /* 帧长度（字节） */
    uint8_t sop;                          /* SOP 类型（见 usbpd_sop_e） */
    uint32_t timestamp_ms;                /* 接收时间戳（毫秒） */
};

/** @brief PHY 层全局运行上下文（含 DMA 缓冲、环形队列、中断共享状态） */
struct usbpd_phy_context_t
{
    struct usbpd_phy_rx_t rx[USBPD_RX_DEPTH];     /* 接收帧环形缓冲 */
    uint8_t tx_dma[USBPD_MAX_FRAME_LEN] __attribute__((aligned(4)));   /* TX DMA 源缓冲 */
    uint8_t rx_dma[USBPD_TRACE_FRAME_LEN] __attribute__((aligned(4)));  /* RX DMA 目标缓冲 */
    uint8_t goodcrc_dma[2] __attribute__((aligned(4)));                 /* 自动 GoodCRC 发送缓冲（2 字节头） */
    struct
    {
        uint8_t raw[2];
        uint8_t sop;
        uint32_t timestamp_ms;
    } goodcrc_trace[USBPD_RX_DEPTH];                /* GoodCRC 自动回复 trace 环形缓冲（供观察器刷写） */
    volatile uint8_t rx_read;                       /* RX 环形缓冲读指针 */
    volatile uint8_t rx_write;                      /* RX 环形缓冲写指针 */
    volatile uint8_t rx_count;                      /* RX 环形缓冲有效帧计数 */
    volatile uint8_t goodcrc_read;                  /* GoodCRC trace 环形缓冲读指针 */
    volatile uint8_t goodcrc_write;                 /* GoodCRC trace 环形缓冲写指针 */
    volatile uint8_t goodcrc_count;                 /* GoodCRC trace 环形缓冲有效计数 */
    volatile uint8_t tx_busy;                       /* TX 正在发送标志（1=忙） */
    volatile uint8_t selected_cc : 1;               /* 当前活跃 CC 通道（0=CC1，1=CC2） */
    volatile uint8_t rx_overflow : 1;               /* RX 环形缓冲溢出标志（1=发生溢出） */
    volatile uint8_t power_role : 1;                /* 协议电源角色（1=Source，用于 GoodCRC 头填充） */
    volatile uint8_t data_role : 1;                 /* 协议数据角色（1=DFP，用于 GoodCRC 头填充） */
    volatile uint8_t reserved : 4;                  /* 保留位 */
    uint32_t now_ms;                                 /* 最近一次 task 调度时的系统时间（毫秒） */
    uint32_t tx_deadline_ms;                        /* TX 超时截止时间（毫秒） */
    uint16_t rp_bits;                               /* Rp 电流档位寄存器值（CC_PU_80/180/330） */
};

static struct usbpd_phy_context_t g_dev;  /* PHY 层全局上下文单例 */

/* ===== 内部辅助函数 ===== */
/**
 * @brief  将 USB PD SOP 类型映射为 CH32X035 USBPD 控制器的 TX_SEL 寄存器值
 * @param sop  USB PD 标准 SOP 常量（见 usbpd_sop_e）
 * @return CH32X035 UPD_SOP0/1/2/HARD_RESET/CABLE_RESET 寄存器值；非法 sop 返回 0
 */
static uint8_t device_tx_sel(uint8_t sop)
{
    switch (sop)
    {
    case USBPD_SOP:
        return UPD_SOP0;
    case USBPD_SOP_PRIME:
        return UPD_SOP1;
    case USBPD_SOP_DPRIME:
        return UPD_SOP2;
    case USBPD_SOP_HARD_RESET:
        return UPD_HARD_RESET;
    case USBPD_SOP_CABLE_RESET:
        return UPD_CABLE_RESET;
    default:
        return 0U;
    }
}

/**
 * @brief  从 USBPD STATUS 寄存器的 RX 状态位解码接收帧的 SOP 类型
 *
 * SOP1_HRST / SOP2_CRST 需根据 BMC_BYTE_CNT 区分 HardReset / CableReset（短帧）与 SOP'/SOP''（长帧）。
 *
 * @param status  USBPD STATUS 寄存器值
 * @param length  BMC 接收字节数
 * @return 解码后的 SOP 类型；非法返回 USBPD_SOP_INVALID
 */
static uint8_t device_sop_from_status(uint16_t status, uint8_t length)
{
    switch (status & MASK_PD_STAT)
    {
    case PD_RX_SOP0:
        return USBPD_SOP;
    case PD_RX_SOP1_HRST:
        return (length >= 6U) ? USBPD_SOP_PRIME : USBPD_SOP_HARD_RESET;
    case PD_RX_SOP2_CRST:
        return (length >= 6U) ? USBPD_SOP_DPRIME : USBPD_SOP_CABLE_RESET;
    default:
        return USBPD_SOP_INVALID;
    }
}

/** @brief  使能 USBPD 接收通路（清中断标志、打开 RX 中断、使能 DMA、启动 BMC） */
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

/** @brief  清除两路 CC 端口的 LVE（VCONN 发射使能）位 */
static void device_clear_lve(void)
{
    USBPD->PORT_CC1 &= ~CC_LVE;
    USBPD->PORT_CC2 &= ~CC_LVE;
}

/** @brief  BMC 发送完成收尾：清 tx_busy、关 LVE、恢复接收通路 */
static void device_finish_tx(void)
{
    g_dev.tx_busy = 0U;
    device_clear_lve();
    device_enable_rx();
}

/**
 * @brief  启动一次 BMC 帧发送（配置 TX_SEL / BMC_TX_SZ / DMA 后置 PD_TX_EN + BMC_START）
 *
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param data  发送数据 DMA 缓冲地址
 * @param length  发送长度（字节）
 */
static void device_start_tx(uint8_t sop, uint8_t *data, uint8_t length)
{
    USBPD->CONFIG |= IE_TX_END;
    if (g_dev.selected_cc != 0U)
    {
        USBPD->PORT_CC2 |= CC_LVE;
    }
    else
    {
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

/**
 * @brief  将 DMA 中已接收的帧入队到 rx 环形缓冲（溢出时丢弃最旧帧）
 *
 * @param sop  帧所属 SOP
 * @param length  帧长度（字节）
 */
static void device_queue_rx(uint8_t sop, uint8_t length)
{
    struct usbpd_phy_rx_t *slot;
    if (g_dev.rx_count == USBPD_RX_DEPTH)
    {
        g_dev.rx_read = (uint8_t)((g_dev.rx_read + 1U) % USBPD_RX_DEPTH);
        g_dev.rx_count--;
        g_dev.rx_overflow = 1U;
    }
    slot = &g_dev.rx[g_dev.rx_write];
    slot->length = length;
    slot->sop = sop;
    slot->timestamp_ms = g_dev.now_ms;
    if (length != 0U)
    {
        memcpy(slot->raw, g_dev.rx_dma, length);
    }
    g_dev.rx_write = (uint8_t)((g_dev.rx_write + 1U) % USBPD_RX_DEPTH);
    g_dev.rx_count++;
}

/**
 * @brief  将自动 GoodCRC 回复的报文头入队到 goodcrc_trace 缓冲（供观察器刷写）
 * @param sop  GoodCRC 所属 SOP
 */
static void device_queue_goodcrc_trace(uint8_t sop)
{
    uint8_t write = g_dev.goodcrc_write;

    if (g_dev.goodcrc_count == USBPD_RX_DEPTH)
    {
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

/**
 * @brief  IRQ 中断处理的自动 GoodCRC 应答
 *
 * 解析原始帧的 revision / message_id，按本端角色填充 power_role / data_role，
 * 然后立即启动 2 字节 BMC 发送（不等协议层）。
 *
 * @param sop  接收帧所属 SOP
 * @param raw  接收帧原始字节（至少 2 字节头）
 */
static void device_send_goodcrc(uint8_t sop, const uint8_t *raw)
{
    union usbpd_header_u rx;
    union usbpd_header_u tx;
    if ((sop != USBPD_SOP) && (sop != USBPD_SOP_PRIME) && (sop != USBPD_SOP_DPRIME))
    {
        return;
    }
    rx.bytes[0] = raw[0];
    rx.bytes[1] = raw[1];
    tx.raw = 0U;
    tx.bits.type = USBPD_CTRL_GOODCRC;
    tx.bits.revision = rx.bits.revision;
    tx.bits.message_id = rx.bits.message_id;
    if (sop == USBPD_SOP)
    {
        tx.bits.power_role = g_dev.power_role;
        tx.bits.data_role = g_dev.data_role;
    }
    g_dev.goodcrc_dma[0] = tx.bytes[0];
    g_dev.goodcrc_dma[1] = tx.bytes[1];
    g_dev.tx_busy = 1U;
    g_dev.tx_deadline_ms = g_dev.now_ms + USBPD_TX_TIMEOUT_MS;
    device_queue_goodcrc_trace(sop);
    device_start_tx(sop, g_dev.goodcrc_dma, 2U);
}

/**
 * @brief  判断一帧是否为 GoodCRC 控制消息（data_objects=0 且 type=USBPD_CTRL_GOODCRC）
 * @param raw  原始帧字节
 * @param length  帧长度
 * @return 1=GoodCRC，0=其他或参数异常
 */
static uint8_t device_is_goodcrc(const uint8_t *raw, uint8_t length)
{
    union usbpd_header_u header;

    if ((raw == 0) || (length < 2U))
    {
        return 0U;
    }

    header.bytes[0] = raw[0];
    header.bytes[1] = raw[1];
    return ((header.bits.data_objects == 0U) && (header.bits.type == USBPD_CTRL_GOODCRC));
}

/**
 * @brief  通过内部比较器读取 CC 端口电压，映射为 usbpd_cc_e 枚举
 *
 * 逐档切换比较器阈值（22→45→55→66→95→123 mV），结合 CC_PD 位判断：
 *   CC_PD 置位（Source 上拉）：≥123mV=RP_3A，≥66mV=RP_1A5，≥22mV=RP_DEF，否则 Open；
 *   CC_PD 未置位（Sink 下拉）：≥22mV=RD，否则 RA。
 *
 * @param port  对应 CC 端口寄存器地址指针（&USBPD->PORT_CC1 或 &USBPD->PORT_CC2）
 * @return 检测结果（见 usbpd_cc_e）
 */
static enum usbpd_cc_e device_read_cc(volatile uint16_t *port)
{
    static const uint16_t cmp_reg[] = {CC_CMP_22, CC_CMP_45, CC_CMP_55, CC_CMP_66, CC_CMP_95, CC_CMP_123};
    static const uint16_t cmp_mv[] = {22U, 45U, 55U, 66U, 95U, 123U};
    uint16_t voltage = 0U;
    uint8_t i;

    /* PA_CC_AI belongs to the USBPD CC comparator, not GPIOC->INDR. */
    for (i = 0U; i < (uint8_t)(sizeof(cmp_reg) / sizeof(cmp_reg[0])); ++i)
    {
        *port &= (uint16_t)~(CC_CMP_Mask | PA_CC_AI);
        *port |= cmp_reg[i];
        Delay_Us(2U);
        if ((*port & PA_CC_AI) == 0U)
        {
            break;
        }
        voltage = cmp_mv[i];
    }

    *port &= (uint16_t)~(CC_CMP_Mask | PA_CC_AI);
    *port |= CC_CMP_66;

    if ((*port & CC_PD) != 0U)
    {
        if (voltage >= 123U)
        {
            return USBPD_CC_RP_3A;
        }
        if (voltage >= 66U)
        {
            return USBPD_CC_RP_1A5;
        }
        if (voltage >= 22U)
        {
            return USBPD_CC_RP_DEF;
        }
        return USBPD_CC_OPEN;
    }

    return (voltage >= 22U) ? USBPD_CC_RD : USBPD_CC_RA;
}

/* ===== 对外接口 ===== */
/** @brief  选择当前活跃的 CC 通道（0=CC1，非 0=CC2，切换 USBPD CONFIG 的 CC_SEL 位） */
void usbpd_phy_set_cc(uint8_t cc)
{
    g_dev.selected_cc = (cc == 2U);
    if (g_dev.selected_cc != 0U)
    {
        USBPD->CONFIG |= CC_SEL;
    }
    else
    {
        USBPD->CONFIG &= ~CC_SEL;
    }
}

/** @brief  设置 CC 引脚上拉/下拉模式（RD=Sink，RP=Source，其他=悬空） */
void usbpd_phy_set_pull(uint8_t pull)
{
    if (pull == USBPD_CC_PULL_RD)
    {
        USBPD->PORT_CC1 = CC_CMP_66 | CC_PD;
        USBPD->PORT_CC2 = CC_CMP_66 | CC_PD;
    }
    else if (pull == USBPD_CC_PULL_RP)
    {
        USBPD->PORT_CC1 = CC_CMP_66 | g_dev.rp_bits;
        USBPD->PORT_CC2 = CC_CMP_66 | g_dev.rp_bits;
    }
    else
    {
        USBPD->PORT_CC1 = CC_CMP_66;
        USBPD->PORT_CC2 = CC_CMP_66;
    }
}

/** @brief  设置 Source Rp 电流档位（≥3A→330Ω，≥1.5A→180Ω，默认 80Ω；受 CC_PU_80/CC_PU_180 宏影响） */
void usbpd_phy_set_rp(uint32_t current_ma)
{
#if defined(CC_PU_80) && defined(CC_PU_180)
    if (current_ma >= 3000U)
        g_dev.rp_bits = CC_PU_330;
    else if (current_ma >= 1500U)
        g_dev.rp_bits = CC_PU_180;
    else
        g_dev.rp_bits = CC_PU_80;
#else
    (void)current_ma;
    g_dev.rp_bits = CC_PU_330;
#endif
}

/** @brief  通知 PHY 层协议角色（用于 GoodCRC 报文头填充 role 位） */
void usbpd_phy_set_roles(uint8_t power_role, uint8_t data_role)
{
    g_dev.power_role = (power_role != 0U) ? 1U : 0U;
    g_dev.data_role = (data_role != 0U) ? 1U : 0U;
}

/** @brief  读取两路 CC 引脚状态（通过内部比较器逐档阈值检测） */
void usbpd_phy_get_cc(enum usbpd_cc_e *cc1, enum usbpd_cc_e *cc2)
{
    if ((cc1 == 0) || (cc2 == 0))
    {
        return;
    }

    *cc1 = device_read_cc(&USBPD->PORT_CC1);
    *cc2 = device_read_cc(&USBPD->PORT_CC2);
}

/** @brief  读取 VBUS 电压（复用 adc_get_vbus_mv()，<800mV 返回 BUSY） */
int usbpd_phy_get_vbus(uint32_t *mv)
{
    int value;
    if (mv == 0)
        return USBPD_ERR;
    value = (int)adc_get_vbus_mv();
    *mv = (value > 0) ? (uint32_t)value : 0U;
    return (*mv >= 800U) ? USBPD_OK : USBPD_BUSY;
}

/** @brief  发起 USB PD 帧发送（拷贝到 tx_dma 后启动 BMC，tx_busy 期间返回 BUSY） */
int usbpd_phy_send(uint8_t sop, const uint8_t *raw, uint8_t len, uint32_t now_ms)
{
    if ((sop > USBPD_SOP_CABLE_RESET) || (len > USBPD_MAX_FRAME_LEN) || ((len != 0U) && (raw == 0)) ||
        (g_dev.tx_busy != 0U))
    {
        return (g_dev.tx_busy != 0U) ? USBPD_BUSY : USBPD_ERR;
    }
    if (len != 0U)
        memcpy(g_dev.tx_dma, raw, len);
    g_dev.tx_busy = 1U;
    g_dev.tx_deadline_ms = now_ms + USBPD_TX_TIMEOUT_MS;
    usbpd_observer_record(MESSAGE_BUFFER_TX, sop, raw, len, now_ms,
                          (sop >= USBPD_SOP_HARD_RESET) ? USBPD_OBSERVER_RESET : 0U);
    device_start_tx(sop, g_dev.tx_dma, len);
    return USBPD_OK;
}

/** @brief  从 RX 环形缓冲弹出原始帧并解码为 usbpd_frame_t（HardReset 跳过解码） */
int usbpd_phy_receive(struct usbpd_frame_t *frame)
{
    struct usbpd_phy_rx_t slot;

    if (frame == 0)
        return USBPD_ERR;

    __disable_irq();
    if (g_dev.rx_count == 0U)
    {
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
    usbpd_observer_record(MESSAGE_BUFFER_RX, slot.sop, slot.raw, slot.length, slot.timestamp_ms,
                          (slot.sop >= USBPD_SOP_HARD_RESET) ? USBPD_OBSERVER_RESET : 0U);
    if (slot.sop >= USBPD_SOP_HARD_RESET)
        return USBPD_OK;
    return usbpd_frame_decode_wire(slot.raw, slot.length, slot.sop, frame);
}

/** @brief  查询 BMC 发送 DMA 是否空闲 */
uint8_t usbpd_phy_tx_idle(void)
{
    return (g_dev.tx_busy == 0U);
}

/** @brief  原子性地取出并清除 RX 环形缓冲溢出标志 */
uint8_t usbpd_phy_take_rx_overflow(void)
{
    uint8_t overflow;

    __disable_irq();
    overflow = g_dev.rx_overflow;
    g_dev.rx_overflow = 0U;
    __enable_irq();
    return overflow;
}

/** @brief  复位 RX 环形缓冲读写指针（关中断操作） */
void usbpd_phy_reset_rx(void)
{
    __disable_irq();
    g_dev.rx_read = g_dev.rx_write = g_dev.rx_count = 0U;
    __enable_irq();
}

/**
 * @brief  初始化 PHY 层（GPIO / 时钟 / USBPD 控制器 / NVIC）
 *
 * 初始化后默认配置为 Sink 模式（CC_PULL_RD），待策略层调用 set_rp / set_pull / set_roles。
 */
int usbpd_phy_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    memset(&g_dev, 0, sizeof(g_dev));
    g_dev.rp_bits = CC_PU_330;
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

/** @brief  PHY 层周期任务：TX 超时强停 + GoodCRC trace 刷写至观察器 */
void usbpd_phy_task(uint32_t now_ms)
{
    g_dev.now_ms = now_ms;
    if ((g_dev.tx_busy != 0U) && ((int32_t)(now_ms - g_dev.tx_deadline_ms) >= 0))
    {
        USBPD->CONTROL &= (uint16_t)~(PD_TX_EN | BMC_START);
        USBPD->STATUS |= IF_TX_END;
        device_finish_tx();
    }

    for (;;)
    {
        uint8_t read;
        uint8_t raw[2];
        uint8_t sop;
        uint32_t timestamp_ms;

        __disable_irq();
        if (g_dev.goodcrc_count == 0U)
        {
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

        usbpd_observer_record(MESSAGE_BUFFER_TX, sop, raw, 2U, timestamp_ms, USBPD_OBSERVER_GOODCRC);
    }
}

/**
 * @brief  CH32X035 USBPD 中断服务函数（快速中断）
 *
 * 处理三类中断标志：
 *   IF_RX_ACT  → 读取 BMC_BYTE_CNT 与 STATUS，解码 SOP，自动 GoodCRC 应答（非 GoodCRC 帧）并入队；
 *   IF_TX_END  → 调用 device_finish_tx() 复位发送状态；
 *   IF_RX_RESET → 仅清标志（控制器内部复位）。
 */
void USBPD_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBPD_IRQHandler(void)
{
    uint16_t status = USBPD->STATUS;
    if ((status & IF_RX_ACT) != 0U)
    {
        uint8_t length;
        uint8_t sop;
        USBPD->STATUS |= IF_RX_ACT;
        length = (uint8_t)USBPD->BMC_BYTE_CNT;
        if (length > USBPD_TRACE_FRAME_LEN)
            length = USBPD_TRACE_FRAME_LEN;
        sop = device_sop_from_status(status, length);
        if (sop != USBPD_SOP_INVALID)
        {
            if ((sop <= USBPD_SOP_DPRIME) && (length >= 6U) && (device_is_goodcrc(g_dev.rx_dma, length) == 0U))
            {
                Delay_Us(30U);
                device_send_goodcrc(sop, g_dev.rx_dma);
            }
            device_queue_rx(sop, length);
        }
    }
    if ((status & IF_TX_END) != 0U)
    {
        USBPD->STATUS |= IF_TX_END;
        device_finish_tx();
    }
    if ((status & IF_RX_RESET) != 0U)
    {
        USBPD->STATUS |= IF_RX_RESET;
    }
}
