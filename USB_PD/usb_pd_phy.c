/**
 * @file usb_pd_phy.c
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief USB Power Delivery 物理层实现
 * @version 0.1
 * @date 2025-12-08
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "usb_pd_def.h"
#include "usb_pd_phy.h"

#include <string.h>

#include "ch32l103.h"
#include "ch32l103_usbpd.h"
#include "timer.h"

#define PDDEBUG

#ifdef PDDEBUG
#define PDPRINT(format, ...) PRINT (format, ##__VA_ARGS__)
#else
#define PDPRINT(X...)
#endif


/**
 * @brief 物理层连接元数据
 */
static struct pd_phy_t
{
    enum usbpd_cc_voltage_status_e cc1_volt_status;
    enum usbpd_cc_voltage_status_e cc2_volt_status;
    uint8_t is_ready;
    uint8_t is_tx_busy;
    uint16_t buf_cnt;
    enum usbpd_sop_type_e rx_sop;
    uint64_t tx_timeout; /**< 非阻塞发送超时时间戳 */
} pd = {0};

__attribute__((aligned(4))) uint8_t usb_pd_rx_buf[40];
__attribute__((aligned(4))) uint8_t usb_pd_tx_buf[40];

int usb_phy_get_vbus(int *mv)
{
    *mv = (int)adc_get_vbus_mv();
    if (*mv < CONFIG_USB_PDVBUS_SAFE_0V)
        return USBPD_BUSY ;
    else
        return USBPD_OK;
}

/**
 * @brief  扫描 CC 线电平状态
 * @param  port: 端口号
 * @param  cc1:  输出 CC1 状态指针
 * @param  cc2:  输出 CC2 状态指针
 */
void usb_pd_phy_get_cc_state(int port, enum usbpd_cc_voltage_status_e *cc1, enum usbpd_cc_voltage_status_e *cc2)
{
    const uint16_t cc_cmp_reg[] = {CC_CMP_22, CC_CMP_45, CC_CMP_55, CC_CMP_66, CC_CMP_95, CC_CMP_123};
    const uint16_t cc_cmp_list[] = {22, 45, 55, 66, 95, 123};

    enum usbpd_cc_voltage_status_e *cc = NULL;
    volatile uint16_t *PORT_CC = NULL;
    uint32_t cc_pin = 0;
    uint16_t volt = 0;

    for (uint8_t i = 0; i < 2; i++)
    {
        cc = (i == 0) ? cc1 : cc2;
        cc_pin = (i == 0) ? PIN_CC1 : PIN_CC2;
        PORT_CC = (i == 0) ? &USBPD->PORT_CC1 : &USBPD->PORT_CC2;
        volt = 0;

        if ((GPIOC->INDR & cc_pin) != (uint32_t)Bit_RESET)
        {
            *cc = TYPEC_CC_VOLT_OPEN;
        }
        else
        {
            for (uint8_t j = 0; j < 6; j++)
            {
                *PORT_CC &= ~(CC_CMP_Mask | PA_CC_AI);
                *PORT_CC |= cc_cmp_reg[j];
                Delay_Us(2);

                if (*PORT_CC & PA_CC_AI)
                {
                    volt = cc_cmp_list[j];
                }
                else
                {
                    break;
                }
            }

            if (*PORT_CC & CC_PD)
            {
                if (volt >= 123)
                    *cc = TYPEC_CC_VOLT_RP_3_0;
                else if (volt >= 66)
                    *cc = TYPEC_CC_VOLT_RP_1_5;
                else if (volt >= 22)
                    *cc = TYPEC_CC_VOLT_RP_DEF;
                else
                    *cc = TYPEC_CC_VOLT_OPEN;
            }
            else
            {
                if (volt >= 22)
                    *cc = TYPEC_CC_VOLT_RD;
                else
                    *cc = TYPEC_CC_VOLT_RA;
            }
        }

        *PORT_CC &= ~(CC_CMP_Mask | PA_CC_AI);
        *PORT_CC |= CC_CMP_66;
    }
}

/**
 * @brief  根据连接结果更新硬件 CC 极性选择
 */
void usb_pd_phy_set_sel(uint8_t cc_idx)
{
    if (cc_idx == 1)
    {
        USBPD->CONFIG &= ~CC_SEL; // 选择 CC1
    }
    else if (cc_idx == 2)
    {
        USBPD->CONFIG |= CC_SEL; // 选择 CC2
    }
}

/**
 * @brief  配置 CC 引脚的上下拉电阻
 * @param  pull: 上拉(Source)、下拉(Sink)或悬空
 */
void usb_pd_phy_set_pull(enum usbpd_cc_pull_config_e pull)
{

    if (pull == TYPEC_CC_PULL_RD)
    {
        USBPD->PORT_CC1 = CC_CMP_66 | CC_PD;
        USBPD->PORT_CC2 = CC_CMP_66 | CC_PD;
    }
    else if (pull == TYPEC_CC_PULL_RP)
    {
        USBPD->PORT_CC1 = CC_CMP_66 | CC_PU_330;
        USBPD->PORT_CC2 = CC_CMP_66 | CC_PU_330;
    }
    else
    {
        USBPD->PORT_CC1 = CC_CMP_66;
        USBPD->PORT_CC2 = CC_CMP_66;
    }
}

void usb_pd_phy_reset(void)
{
    /* TODO: 实现 PD PHY 复位逻辑（BMC 收发器复位、DMA 重置等） */
}

/**
 * @brief 使能 PD 接收
 *
 */
void usb_pd_phy_enable_rx(void)
{
    USBPD->CONFIG |= PD_ALL_CLR;
    USBPD->CONFIG &= ~PD_ALL_CLR;
    USBPD->CONFIG |= IE_RX_ACT | IE_RX_RESET | PD_DMA_EN;
    USBPD->DMA = (uint32_t)(uint8_t *)usb_pd_rx_buf;
    USBPD->CONTROL &= ~PD_TX_EN;
    USBPD->BMC_CLK_CNT = UPD_TMR_RX_48M;
    USBPD->CONTROL |= BMC_START;
    NVIC_EnableIRQ(USBPD_IRQn);
}

/**
 * @brief  获取 PD 消息
 * @param  pd_buf: 消息缓冲区指针
 * @return 0: 成功获取消息, 1: 无有效数据或校验错误
 */
int usb_pd_phy_get_msg(struct usbpd_phy_buffer_t *pd_buf)
{

    if (!pd.is_ready || !pd_buf)
    {
        return USBPD_BUSY;
    }

    pd_buf->sop = pd.rx_sop;

    pd_buf->header.d16 = (usb_pd_rx_buf[0] | (usb_pd_rx_buf[1] << 8));

    pd_buf->payload_len = pd_buf->header.msg_header.n_data_obj;

    if (pd_buf->payload_len > 0)
    {
        uint32_t data_len = pd_buf->payload_len * 4;
        memcpy(pd_buf->payload, &usb_pd_rx_buf[2], data_len);
    }
  
    pd_buf->flag.is_ready = 1;
    
#ifdef PDDEBUG
    PDPRINT("[PD] > Raw:");
    for (uint32_t i = 0; i < pd.buf_cnt; i++)
    {
        PDPRINT(" %02X", usb_pd_rx_buf[i]);
    }
    PDPRINT("\n");
#endif
    pd.is_ready = 0;

    return USBPD_OK;
}

/**
 * @brief  发送 PD 消息
 * @param  pd_buf: 待发送消息缓冲区指针
 * @return 0: 发送启动成功, 1: PHY忙或参数错误
 */
int usb_pd_phy_send_msg(struct usbpd_phy_buffer_t *pd_buf)
{
    if (!pd_buf || !pd_buf->flag.is_ready || pd_buf->payload_len > 7)
    {
        return USBPD_ERR;
    }

    if (pd.is_tx_busy) 
    {
        return USBPD_BUSY;
    }

    pd_buf->flag.is_ready = 0;
    
    usb_pd_tx_buf[0] = (uint8_t)(pd_buf->header.d16 & 0xFF);
    usb_pd_tx_buf[1] = (uint8_t)((pd_buf->header.d16 >> 8) & 0xFF);


    if (pd_buf->payload_len > 0)
    {
        uint32_t data_len = pd_buf->payload_len * 4;
        memcpy(&usb_pd_tx_buf[2], pd_buf->payload, data_len);
    }

    uint32_t total_len = 2 + (pd_buf->payload_len * 4);

#ifdef PDDEBUG
    PDPRINT("[PD] < Send Raw:");
    for (uint32_t i = 0; i < total_len; i++)
    {
        PDPRINT(" %02X", usb_pd_tx_buf[i]);
    }
    PDPRINT("\n");
#endif

    pd.is_tx_busy = 1; 
    usb_pd_phy_send_packet(0, usb_pd_tx_buf, total_len, pd_buf->sop);

    return USBPD_OK;
}

/**
 * @brief  发送 PD 报文包
 * @param  wait: 是否阻塞等待发送完成
 * @param  buf: 发送数据缓冲区指针
 * @param  len: 发送长度
 * @param  sop: SOP 类型 (SOP/SOP'/HardReset等)
 */
void usb_pd_phy_send_packet(uint8_t wait, uint8_t *buf, uint16_t len, enum usbpd_sop_type_e sop)
{

    uint8_t txsel;

    switch (sop)
    {
    case USBPD_SOP_TYPE_SOP:
        txsel = UPD_SOP0;
        break;
    case USBPD_SOP_TYPE_SOP_PRIME:
        txsel = UPD_SOP1;
        break;
    case USBPD_SOP_TYPE_SOP_DOUBLE_PRIME:
        txsel = UPD_SOP2;
        break;
    case USBPD_SOP_TYPE_HARD_RESET:
        txsel = UPD_HARD_RESET;
        break;
    case USBPD_SOP_TYPE_CABLE_RESET:
        txsel = UPD_CABLE_RESET;
        break;
    default:
        txsel = USBPD_SOP_TYPE_INVALID;
        break;
    }

    USBPD->CONFIG |= IE_TX_END;
    if ((USBPD->CONFIG & CC_SEL) == CC_SEL)
    {
        USBPD->PORT_CC2 |= CC_LVE;
    }
    else
    {
        USBPD->PORT_CC1 |= CC_LVE;
    }

    USBPD->BMC_CLK_CNT = UPD_TMR_TX_48M;
    USBPD->DMA = (uint32_t)(uint8_t *)buf;
    USBPD->TX_SEL = txsel;
    USBPD->BMC_TX_SZ = len;
    USBPD->CONTROL |= PD_TX_EN;
    USBPD->STATUS &= BMC_AUX_INVALID;
    USBPD->CONTROL |= BMC_START;

    if (wait)
    {
        while ((USBPD->STATUS & IF_TX_END) == 0)
        {
            __NOP();
        }
        USBPD->STATUS |= IF_TX_END;
        if ((USBPD->CONFIG & CC_SEL) == CC_SEL)
        {
            USBPD->PORT_CC2 &= ~CC_LVE;
        }
        else
        {
            USBPD->PORT_CC1 &= ~CC_LVE;
        }

        usb_pd_phy_enable_rx();
    }
    else
    {
        /* 非阻塞模式：设置 5ms 超时保护 */
        pd.tx_timeout = 5000; /* 5ms */
    }
}

/**
 * @brief PHY 层轮询函数 (TX 超时保护)
 *
 * 检查非阻塞发送是否超时。若超时，强制终止发送：
 * 清除 PD_TX_EN、复位 BMC 状态、重置 is_tx_busy 标志。
 */
void usb_pd_phy_poll(void)
{
    if (pd.is_tx_busy && pd.tx_timeout > 0) {
        if (pd.tx_timeout > 0) {
            pd.tx_timeout--;
        }

        if (pd.tx_timeout == 0) {
            /* 超时：强制终止发送 */
            USBPD->CONTROL &= ~PD_TX_EN;
            USBPD->CONTROL &= ~BMC_START;
            USBPD->STATUS |= IF_TX_END;

            /* 清除 CC_LVE */
            if ((USBPD->CONFIG & CC_SEL) == CC_SEL) {
                USBPD->PORT_CC2 &= ~CC_LVE;
            } else {
                USBPD->PORT_CC1 &= ~CC_LVE;
            }

            pd.is_tx_busy = 0;

#ifdef PDDEBUG
            PDPRINT("[PD] PHY TX timeout, forced abort\n");
#endif

            /* 恢复接收 */
            usb_pd_phy_enable_rx();
        }
    }
}

/**
 * @brief 初始化 PD 物理层
 *
 */
int usb_pd_phy_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBPD, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    AFIO->CTLR |= USBPD_IN_HVT | USBPD_PHY_V33;
    USBPD->CONFIG = PD_DMA_EN;
    USBPD->STATUS = BUF_ERR | IF_RX_BIT | IF_RX_BYTE | IF_RX_ACT | IF_RX_RESET | IF_TX_END;
    USBPD->PORT_CC1 &= ~CC_LVE;
    USBPD->PORT_CC2 &= ~CC_LVE;

    // usb_pd_phy_reset();
    usb_pd_phy_set_pull(TYPEC_CC_PULL_RD);
    usb_pd_phy_enable_rx();

    return USBPD_OK;
}

/**
 * @brief  USB PD 中断处理函数
 */
void USBPD_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void USBPD_IRQHandler(void)
{
    uint8_t ack_buf[2];

    if (USBPD->STATUS & IF_RX_ACT)
    {
        USBPD->STATUS |= IF_RX_ACT;
        if ((USBPD->STATUS & MASK_PD_STAT) != 0)
        {
            pd.is_ready = 1;
            pd.buf_cnt = USBPD->BMC_BYTE_CNT;

            /* 提取 SOP 类型供上层协议层使用 */
            switch (USBPD->STATUS & BMC_AUX_Mask) {
            case BMC_AUX_INVALID:
                pd.rx_sop = USBPD_SOP_TYPE_INVALID;
                break;
            case BMC_AUX_SOP0:
                pd.rx_sop = USBPD_SOP_TYPE_SOP;
                break;
            case BMC_AUX_SOP1_HRST:
                pd.rx_sop = USBPD_SOP_TYPE_HARD_RESET;
                break;
            case BMC_AUX_SOP2_CRST:
                pd.rx_sop = USBPD_SOP_TYPE_CABLE_RESET;
                break;
            default:
                pd.rx_sop = USBPD_SOP_TYPE_INVALID;
                break;
            }
        }

        if ((USBPD->STATUS & MASK_PD_STAT) == PD_RX_SOP0)
        {

            if (USBPD->BMC_BYTE_CNT >= 6)
            {

                if ((USBPD->BMC_BYTE_CNT != 6) || ((usb_pd_rx_buf[0] & 0x1F) != DEF_TYPE_GOODCRC))
                {
                    Delay_Us(30);
                    ack_buf[0] = 0x41;
                    ack_buf[1] = (usb_pd_rx_buf[1] & 0x0E);
                    usb_pd_phy_send_packet(0, ack_buf, 2, USBPD_SOP_TYPE_SOP);
                }
            }
        }
        break;
    }

    if (USBPD->STATUS & IF_TX_END)
    {
        USBPD->PORT_CC1 &= ~CC_LVE;
        USBPD->PORT_CC2 &= ~CC_LVE;
        pd.is_tx_busy = 0;

        USBPD->STATUS |= IF_TX_END;

        usb_pd_phy_enable_rx();
    }

    if (USBPD->STATUS & IF_RX_RESET)
    {
        USBPD->STATUS |= IF_RX_RESET;
    }
}
