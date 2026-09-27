/**
 * @file usb_pd_protocol.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-12-14
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef USB_PD_PROTOCOL_H
#define USB_PD_PROTOCOL_H


#include "usb_pd_def.h"


// clang-format off

/* 微秒时间单位 */
#define MSEC         1000
#define SECOND    1000000
#define MINUTE   60000000
#define HOUR   3600000000ull  /* 太大，无法存放在有符号整数中 */


/* --- USB PD 协议定时器定义 --- */

/* 协议协商相关定时器 */
#define USBPD_T_SEND_SOURCE_CAP  (100) // *MSEC /* 源端发送Source Capabilities间隔: 100-200ms之间 */
#define USBPD_T_SINK_WAIT_CAP    (600) // *MSEC /* 受电端等待能力消息超时: 310-620ms之间 */
#define USBPD_T_SINK_TRANSITION   (35) // *MSEC /* 受电端电源转换时间: 20-35ms之间 */
#define USBPD_T_SOURCE_ACTIVITY   (45) // *MSEC /* 源端活动保持时间: 40-50ms之间 */
#define USBPD_T_SENDER_RESPONSE   (30) // *MSEC /* 发送方等待响应时间: 24-30ms之间 */
#define USBPD_T_PS_TRANSITION    (500) // *MSEC /* 电源转换总时间: 450-550ms之间 */
#define USBPD_T_PS_SOURCE_ON     (480) // *MSEC /* 源端打开电源时间: 390-480ms之间 */
#define USBPD_T_PS_SOURCE_OFF    (920) // *MSEC /* 源端关闭电源时间: 750-920ms之间 */
#define USBPD_T_PS_HARD_RESET     (25) // *MSEC /* 硬复位脉冲宽度: 25-35ms之间 */
#define USBPD_T_ERROR_RECOVERY    (25) // *MSEC /* 错误恢复等待时间: 固定25ms */
#define USBPD_T_CC_DEBOUNCE      (100) // *MSEC /* CC引脚去抖时间: 100-200ms之间 */
/* 注意：DRP_SNK + DRP_SRC必须在50-100ms之间，占空比30%-70% */
#define USBPD_T_DRP_SNK           (40) // *MSEC /* 作为受电端的切换时间: 40ms (占空比~57%) */
#define USBPD_T_DRP_SRC           (30) // *MSEC /* 作为供电端的切换时间: 30ms */
/* 计算：总周期=70ms(在50-100ms范围内)，占空比=40/70≈57%(在30%-70%范围内) */
#define USBPD_T_DEBOUNCE          (15) // *MSEC /* 通用去抖时间: 10-20ms之间 */
#define USBPD_T_SINK_ADJ          (55) // *MSEC /* 受电端调整时间: 在PD_T_DEBOUNCE和60ms之间 */
#define USBPD_T_SRC_RECOVER      (760) // *MSEC /* 源端恢复时间: 660-1000ms之间 */
#define USBPD_T_SRC_RECOVER_MAX (1000) // *MSEC /* 源端恢复最大时间: 1000ms */
#define USBPD_T_SRC_TURN_ON      (275) // *MSEC /* 源端打开电源延迟: 固定275ms */
#define USBPD_T_SAFE_0V          (650) // *MSEC /* 安全0V等待时间: 固定650ms（防止触电） */
#define USBPD_T_NO_RESPONSE     (5500) // *MSEC /* 无响应超时: 4.5-5.5秒之间 */
#define USBPD_T_BIST_TRANSMIT     (50) // *MSEC /* BIST传输时间: 固定50ms（任务等待参数） */
#define USBPD_T_BIST_RECEIVE      (60) // *MSEC /* BIST处理时间: 固定60ms（最大处理时间） */
#define USBPD_T_VCONN_SOURCE_ON  (100) // *MSEC /* VCONN源端开启时间: 固定100ms */
#define USBPD_T_TRY_SRC          (125) // *MSEC /* Try.SRC状态最大时间: 125ms */
#define USBPD_T_TRY_WAIT         (600) // *MSEC /* TryWait.SNK状态最大时间: 600ms */
#define USBPD_T_SINK_REQUEST     (100) // *MSEC /* 受电端请求间隔: 等待100ms后发送下一个请求 */

// clang-format on

/* --- 协议层函数 --- */

/* Power Delivery (PD) 协议状态枚举
 * 定义了USB PD协议状态机的所有可能状态
 * 根据不同的配置选项，状态集会有所不同
 */
enum usbpd_states_e {
    PD_STATE_DISABLED,         /* PD功能禁用状态 */
    PD_STATE_SUSPENDED,        /* PD挂起/暂停状态 */

#ifdef CONFIG_USB_PD_DUAL_ROLE /* 仅当配置为双角色设备时启用 */
    /* --- 设备模式 (Sink - 受电端) 状态 --- */
    PD_STATE_SNK_DISCONNECTED,          /* 受电端断开连接状态 */
    PD_STATE_SNK_DISCONNECTED_DEBOUNCE, /* 受电端断开连接去抖状态 */
    PD_STATE_SNK_ACCESSORY,             /* 受电端附件模式状态 */
    PD_STATE_SNK_HARD_RESET_RECOVER,    /* 受电端硬复位恢复状态 */
    PD_STATE_SNK_DISCOVERY,             /* 受电端设备发现状态 */
    PD_STATE_SNK_REQUESTED,             /* 受电端电源请求状态 */
    PD_STATE_SNK_TRANSITION,            /* 受电端电源转换状态 */
    PD_STATE_SNK_READY,                 /* 受电端就绪状态 */

    /* --- 受电端角色交换 (Sink Swap) 相关状态 --- */
    PD_STATE_SNK_SWAP_INIT,        /* 受电端交换初始化状态 */
    PD_STATE_SNK_SWAP_SNK_DISABLE, /* 受电端交换中禁用当前受电功能状态 */
    PD_STATE_SNK_SWAP_SRC_DISABLE, /* 受电端交换中禁用供电功能状态 */
    PD_STATE_SNK_SWAP_STANDBY,     /* 受电端交换待机状态 */
    PD_STATE_SNK_SWAP_COMPLETE,    /* 受电端交换完成状态 */
#endif                             /* CONFIG_USB_PD_DUAL_ROLE */

    /* --- 主机模式 (Source - 供电端) 状态 --- */
    PD_STATE_SRC_DISCONNECTED,          /* 供电端断开连接状态 */
    PD_STATE_SRC_DISCONNECTED_DEBOUNCE, /* 供电端断开连接去抖状态 */
    PD_STATE_SRC_ACCESSORY,             /* 供电端附件模式状态 */
    PD_STATE_SRC_HARD_RESET_RECOVER,    /* 供电端硬复位恢复状态 */
    PD_STATE_SRC_STARTUP,               /* 供电端启动状态 */
    PD_STATE_SRC_DISCOVERY,             /* 供电端设备发现状态 */
    PD_STATE_SRC_NEGOTIATE,             /* 供电端协商状态 */
    PD_STATE_SRC_ACCEPTED,              /* 供电端接受状态 */
    PD_STATE_SRC_POWERED,               /* 供电端已供电状态 */
    PD_STATE_SRC_TRANSITION,            /* 供电端电源转换状态 */
    PD_STATE_SRC_READY,                 /* 供电端就绪状态 */
    PD_STATE_SRC_GET_SINK_CAP,          /* 供电端获取受电端能力状态 */
    PD_STATE_DR_SWAP,                   /* 双角色交换状态 (角色互换) */

#ifdef CONFIG_USB_PD_DUAL_ROLE          /* 仅当配置为双角色设备时启用 */
    /* --- 供电端角色交换 (Source Swap) 相关状态 --- */
    PD_STATE_SRC_SWAP_INIT,        /* 供电端交换初始化状态 */
    PD_STATE_SRC_SWAP_SNK_DISABLE, /* 供电端交换中禁用受电功能状态 */
    PD_STATE_SRC_SWAP_SRC_DISABLE, /* 供电端交换中禁用供电功能状态 */
    PD_STATE_SRC_SWAP_STANDBY,     /* 供电端交换待机状态 */

#ifdef CONFIG_USBC_VCONN_SWAP      /* 仅当配置支持VCONN交换时启用 */
    /* --- VCONN (线缆供电) 交换相关状态 --- */
    PD_STATE_VCONN_SWAP_SEND,  /* 发送VCONN交换请求状态 */
    PD_STATE_VCONN_SWAP_INIT,  /* VCONN交换初始化状态 */
    PD_STATE_VCONN_SWAP_READY, /* VCONN交换就绪状态 */
#endif                         /* CONFIG_USBC_VCONN_SWAP */
#endif                         /* CONFIG_USB_PD_DUAL_ROLE */

    /* --- 复位相关状态 --- */
    PD_STATE_SOFT_RESET,         /* 软复位状态 */
    PD_STATE_HARD_RESET_SEND,    /* 发送硬复位状态 */
    PD_STATE_HARD_RESET_EXECUTE, /* 执行硬复位状态 */

#ifdef CONFIG_COMMON_RUNTIME     /* 仅当配置公共运行时支持时启用 */
    /* --- BIST (内置自测试) 相关状态 --- */
    PD_STATE_BIST_RX, /* BIST接收测试状态 */
    PD_STATE_BIST_TX, /* BIST发送测试状态 */
#endif

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE /* 仅当配置双角色自动切换时启用 */
    /* --- 双角色自动切换状态 --- */
    PD_STATE_DRP_AUTO_TOGGLE, /* 双角色自动切换状态 */
#endif

    /* --- 特殊状态值 (非实际状态) --- */
    PD_STATE_COUNT, /* 状态总数 - 用于数组大小定义，不是实际状态 */
};


/**
 * @brief 初始化 USB PD 端口
 *
 */
void usb_pd_init (int port);

/**
 * @brief USB PD 端口循环处理函数
 *
 */
void usb_pd_process (int port);

/**
 * @brief 获取底层 PD 协议状态机当前状态
 *
 * @param port PD 端口号
 * @return enum usbpd_states_e 当前状态机状态
 */
enum usbpd_states_e usb_pd_get_state (int port);

/**
 * @brief 获取 PD 协商结果
 *
 * @param port       PD 端口号
 * @param voltage_mv 输出：协商电压 (mV)
 * @param current_ma 输出：协商电流 (mA)
 * @param power_mw   输出：协商功率 (mW)
 * @param pdo_idx    输出：选中的 PDO 索引
 * @return int USBPD_OK 成功, USBPD_ERR 失败
 */
int usb_pd_get_negotiation (int port, uint32_t *voltage_mv, uint32_t *current_ma, uint32_t *power_mw, uint8_t *pdo_idx);

/**
 * @brief 注册 Source 端 Power Data Objects (PDO)
 *
 * 将传入的 PDO 数组存入指定端口的 source_caps，供 Source 端状态机
 * 在 PD_STATE_SRC_DISCOVERY 发送 Source Capabilities 消息时使用。
 *
 * @param port       PD 端口号
 * @param pdos       PDO 数组指针
 * @param pdo_count  PDO 数量 (1-7)
 * @return int USBPD_OK 成功, USBPD_ERR 失败 (pdo_count 超出范围或 pdos 为空)
 */
int usb_pd_set_source_caps (int port, const union usbpd_pdo_u *pdos, uint8_t pdo_count);

/**
 * @brief 处理接收到的 USB PD 消息，并执行相应操作
 *
 * @param type 消息类型类型
 * @param buf 接收的消息缓冲区
 * @param len 消息长度（字节）
 */
// void usb_pd_handle_message(enum usbpd_tx_token_t type, uint8_t *buf, uint16_t len);

#endif /* USB_PD_PROTOCOL_H */
