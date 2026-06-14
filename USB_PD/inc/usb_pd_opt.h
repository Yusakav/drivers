/**
 * @file usb_pd_opt.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-12-21
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef USB_PD_OPT_H
#define USB_PD_OPT_H


#define CONFIG_USB_PD_PORT_COUNT        1       // pd 端口数量
#define CONFIG_USB_PD_DUAL_ROLE         1       // 双重角色
#define CONFIG_USB_PD_ROLE_DEFAULT      0       // 0:受电端; 1:供电端

#define CONFIG_USB_PDVBUS_SAFE_0V       800   // 0.8V 以下认为是 Safe0V
#define CONFIG_USB_PDVBUS_VSAFE_5V      4000  // 4.0V 以上认为是检测到有效的 VBUS 供电


#endif /* USB_PD_OPT_H */
