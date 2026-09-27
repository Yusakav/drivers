/**
 * @file usb_pd_phy.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief USB Power Delivery 物理层实现
 * @version 0.1
 * @date 2025-12-08
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef USB_PD_PHY_H
#define USB_PD_PHY_H

#include <stdint.h>

#include "usb_pd_def.h"

int usb_phy_get_vbus(int *mv);

/**
 * @brief  根据连接结果更新硬件 CC 极性选择
 */
void usb_pd_phy_set_sel (uint8_t cc_idx);

/**
 * @brief  配置 CC 引脚的上下拉电阻
 * @param  pull: 上拉(Source)、下拉(Sink)或悬空
 */
void usb_pd_phy_set_pull (enum usbpd_cc_pull_config_e pull);

/**
 * @brief  扫描 CC 线
 * 
 * @param port 端口号
 * @param cc1 (CC1) @usbpd_cc_voltage_status_e
 * @param cc2 (CC2) @usbpd_cc_voltage_status_e
 */
void usb_pd_phy_get_cc_state (int port, enum usbpd_cc_voltage_status_e *cc1, enum usbpd_cc_voltage_status_e *cc2);

/**
 * @brief  获取 PD 消息
 * @param  pd_buf: 消息缓冲区指针
 */
int usb_pd_phy_get_msg (struct usbpd_phy_buffer_t *pd_buf);

/**
 * @brief  发送 PD 消息
 * @param  pd_buf: 待发送消息缓冲区指针
 * @return 0: 发送启动成功, 1: PHY忙或参数错误
 */
int usb_pd_phy_send_msg(struct usbpd_phy_buffer_t *pd_buf);

/**
 * @brief  发送 PD 报文包
 * @param  wait: 是否阻塞等待发送完成
 * @param  buf: 发送数据缓冲区指针
 * @param  len: 发送长度
 * @param  sop: SOP 类型 (SOP/SOP'/HardReset等)
 */
void usb_pd_phy_send_packet(uint8_t wait, uint8_t *buf, uint16_t len, enum usbpd_sop_type_e sop);

/**
 * @brief 初始化 PD 物理层
 *
 */
int usb_pd_phy_init(void);

/**
 * @brief PHY 层轮询函数 (TX 超时保护)
 *
 * 检查非阻塞发送是否超时。若超时，强制终止发送：
 * 清除 PD_TX_EN、复位 BMC 状态、重置 is_tx_busy 标志。
 * 应在每个主循环周期调用，推荐在 pd_app_process 的每个 sub_step 开头调用。
 */
void usb_pd_phy_poll(void);

#endif /* USB_PD_PHY_H */
