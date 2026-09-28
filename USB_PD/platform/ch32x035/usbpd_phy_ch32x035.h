/**
* @file usbpd_phy_ch32x035.h
* @brief USB PD PHY 层（CH32X035 硬件）对外接口声明
*
* 所属模块：USB PD 协议栈（PHY/硬件抽象层，绑定 CH32X035 MCU 内置 USBPD 控制器）。
* 对应实现：usbpd_phy_ch32x035.c；依赖寄存器定义：ch32x035_usbpd.h。
*
* PHY 层职责：
*   - CC 线 Rp/Rd 检测、Rp 电流档位设置、VCONN/LVE 开关；
*   - BMC 编码/解码、SOP0/1/2/HardReset/CableReset 发送与接收；
*   - DMA 缓冲与中断驱动的收发环形队列；
*   - 自动 GoodCRC 应答（IRQ 中构造并发送）；
*   - VBUS 电压采集（复用 ADC）。
*
* 调用约定：
*   1. usbpd_phy_init() 初始化 GPIO / 时钟 / USBPD 控制器 / NVIC；
*   2. 协议层通过 usbpd_phy_set_roles / set_rp / set_pull / set_cc 配置硬件；
*   3. usbpd_phy_task() 周期调用处理 TX 超时与 GoodCRC trace 刷写；
*   4. 中断 USBPD_IRQHandler() 处理 RX_ACT / TX_END / RX_RESET 事件。
*/
#ifndef USBPD_PHY_CH32X035_H
#define USBPD_PHY_CH32X035_H

#include "usbpd_def.h"

/**
 * @brief  选择当前活跃的 CC 通道
 *
 * 切换 USBPD 控制器的 CC_SEL 寄存器，使后续 BMC 收发在指定 CC 线上进行。
 *
 * @param cc  0=CC1，非 0=CC2
 */
void usbpd_phy_set_cc(uint8_t cc);

/**
 * @brief  设置 CC 引脚上拉/下拉模式
 *
 * @param pull  见 usbpd_def.h 中 USBPD_CC_PULL_* 常量：
 *              USBPD_CC_PULL_RD=Sink 下拉；USBPD_CC_PULL_RP=Source 上拉；其他=悬空
 */
void usbpd_phy_set_pull(uint8_t pull);

/**
 * @brief  设置 Source Rp 电流档位（影响 CC 上拉电阻值）
 *
 * 根据电流参数选择 80mA / 180mA / 330mA 三档；受 CC_PU_80 / CC_PU_180 宏是否定义影响。
 *
 * @param current_ma  Rp 电流目标（mA）
 */
void usbpd_phy_set_rp(uint32_t current_ma);

/**
 * @brief  通知 PHY 层当前协议角色（用于 GoodCRC 报文头填充 role 位）
 *
 * @param power_role  电源角色（0=Sink，非 0=Source）
 * @param data_role  数据角色（0=UFP，非 0=DFP）
 */
void usbpd_phy_set_roles(uint8_t power_role, uint8_t data_role);

/**
 * @brief  读取两路 CC 引脚状态（Rp/Rd/Ra/Open）
 *
 * 通过内部比较器逐档切换读取电压阈值，返回最接近的 usbpd_cc_e 值。
 *
 * @param cc1  输出 CC1 状态（可为 NULL）
 * @param cc2  输出 CC2 状态（可为 NULL）
 */
void usbpd_phy_get_cc(enum usbpd_cc_e *cc1, enum usbpd_cc_e *cc2);

/**
 * @brief  读取当前 VBUS 电压
 *
 * 通过 adc_get_vbus_mv() 采集；低于 800mV 返回 USBPD_BUSY。
 *
 * @param mv  输出电压值（mV）
 * @return USBPD_OK ≥ 800mV；USBPD_BUSY < 800mV；USBPD_ERR 参数为 NULL
 */
int usbpd_phy_get_vbus(uint32_t *mv);

/**
 * @brief  发起 USB PD 帧发送（同步接口，底层 DMA 异步）
 *
 * 拷贝原始帧到 DMA 缓冲后启动 BMC 发送；tx_busy 期间返回 USBPD_BUSY。
 * HardReset/CableReset 无原始帧（len=0），仍需指定 sop。
 *
 * @param sop  SOP 类型（见 usbpd_sop_e，最大 USBPD_SOP_CABLE_RESET）
 * @param raw  原始帧字节（可为 NULL，len=0 时）
 * @param len  帧长度（字节，最大 USBPD_MAX_FRAME_LEN）
 * @param now_ms  当前系统时间（毫秒，用于 tx_deadline 与观察器时间戳）
 * @return USBPD_OK 发起成功；USBPD_BUSY 上一帧尚未发送完成；USBPD_ERR 参数非法
 */
int usbpd_phy_send(uint8_t sop, const uint8_t *raw, uint8_t len, uint32_t now_ms);

/**
 * @brief  从接收环形缓冲中弹出一条原始帧并解码为 usbpd_frame_t
 *
 * 关中断读取缓冲头，恢复中断后调用 usbpd_frame_decode_wire() 解码。
 * HardReset/CableReset 帧跳过解码直接返回。
 *
 * @param frame  输出解码后的帧结构
 * @return USBPD_OK 成功；USBPD_BUSY 缓冲为空；USBPD_ERR 参数为 NULL
 */
int usbpd_phy_receive(struct usbpd_frame_t *frame);

/**
 * @brief  查询发送 DMA 是否空闲
 * @return 1=空闲；0=发送中
 */
uint8_t usbpd_phy_tx_idle(void);

/**
 * @brief  原子性地取出并清除接收溢出标志
 *
 * @return 1=自上次取出以来发生过溢出；0=未发生
 */
uint8_t usbpd_phy_take_rx_overflow(void);

/** @brief  复位接收环形缓冲指针（关中断操作） */
void usbpd_phy_reset_rx(void);

/**
 * @brief  初始化 PHY 层（GPIO / 时钟 / USBPD 控制器 / NVIC）
 *
 * 初始化后默认配置为 Sink 模式（CC_PULL_RD），待策略层调用 set_rp / set_pull / set_roles。
 *
 * @return USBPD_OK 成功
 */
int usbpd_phy_init(void);

/**
 * @brief  PHY 层周期任务（需在主循环中以毫秒节拍调用）
 *
 * 处理：TX DMA 超时强停、GoodCRC trace 缓冲刷写至观察器。
 *
 * @param now_ms  当前系统时间（毫秒）
 */
void usbpd_phy_task(uint32_t now_ms);

#endif
