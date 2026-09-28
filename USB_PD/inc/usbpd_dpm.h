/**
* @file usbpd_dpm.h
* @brief USB PD 设备策略管理器（DPM）回调接口定义
*
* 所属模块：USB PD 协议栈（策略管理层，连接协议层与应用/板级驱动）。
*
* DPM 角色：
*   - 协议层将接收到的消息、事件通过 usbpd_dpm_callbacks_t 回调上报；
*   - DPM 通过回调控制板级电源输出（Source 模式）、VCONN 开关、放电等；
*   - DPM 可按需响应扩展消息请求（usbpd_dpm_get_extended_fn）。
*
* 本文件仅定义类型与回调签名，不包含任何实现逻辑。
*/
#ifndef USBPD_DPM_H
#define USBPD_DPM_H

#include "usbpd_def.h"

/** @brief 消息分类（DPM 回调分类参数，对应协议层消息头类型） */
enum usbpd_dpm_message_category_e
{
    USBPD_DPM_CONTROL_MESSAGE = 0U, /* 控制消息（data_objects=0） */
    USBPD_DPM_DATA_MESSAGE,         /* 数据消息（data_objects>0 且 extended=0） */
    USBPD_DPM_EXTENDED_MESSAGE,     /* 扩展消息（extended=1） */
};

/** @brief DPM 事件类型（经 usbpd_dpm_event_fn 回调上报应用层） */
enum usbpd_dpm_event_e
{
    USBPD_DPM_ATTACHED = 0U,     /* 端口已连接（Rp 检测完成、协议协商就绪前） */
    USBPD_DPM_DETACHED,          /* 端口已断开（Rp 丢失） */
    USBPD_DPM_CONTRACT,          /* 电源合约建立成功（PS_RDY 收到，value0=电压 mV，value1=电流 mA） */
    USBPD_DPM_CONTRACT_LOST,     /* 电源合约丢失（断开/复位） */
    USBPD_DPM_EPR_ENTERED,      /* 进入 EPR 模式成功 */
    USBPD_DPM_EPR_EXITED,       /* 退出 EPR 模式 */
    USBPD_DPM_HARD_RESET,       /* 收到 Hard Reset 信号 */
    USBPD_DPM_PROTOCOL_ERROR,   /* 协议层错误（TX_TIMEOUT/RX_OVERFLOW/ERROR） */
};

/**
 * @brief  消息接收回调函数类型
 *
 * 协议层每收到一条消息（控制/数据/扩展），先调用此回调让 DPM 消费；
 * 若 DPM 返回 USBPD_ERR_UNSUPPORTED，协议层将按默认策略回复（如 NOT_SUPPORTED）。
 *
 * @param port  端口号（单端口通常为 0）
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param category  消息分类（见 usbpd_dpm_message_category_e）
 * @param type  消息类型（见 usbpd_ctrl_e / usbpd_data_e / usbpd_extended_e）
 * @param payload  消息负载字节
 * @param length  负载长度（字节）
 * @param context  用户上下文（见 usbpd_dpm_callbacks_t.context）
 * @return USBPD_OK 已消费；USBPD_ERR_UNSUPPORTED 未消费（协议层按默认策略回复）
 */
typedef int (*usbpd_dpm_message_fn)(uint8_t port, uint8_t sop, uint8_t category, uint8_t type, const uint8_t *payload,
                                    uint16_t length, void *context);

/**
 * @brief  扩展消息响应填充回调函数类型
 *
 * DPM 需填充指定 type 扩展消息的数据内容，供协议层编码发送。
 *
 * @param port  端口号
 * @param type  扩展消息类型（见 usbpd_extended_e）
 * @param data  输出数据缓冲
 * @param length  输入/输出：缓冲容量 → 实际填充长度
 * @param context  用户上下文
 * @return USBPD_OK 填充成功；USBPD_ERR_UNSUPPORTED 不支持该扩展消息
 */
typedef int (*usbpd_dpm_get_extended_fn)(uint8_t port, uint8_t type, uint8_t *data, uint16_t *length, void *context);

/**
 * @brief  DPM 事件回调函数类型
 *
 * CONTRACT 事件的 value0/value1 携带协商电压/电流；其余事件为 0。
 *
 * @param port  端口号
 * @param event  事件类型（见 usbpd_dpm_event_e）
 * @param value0  事件关联值 0（CONTRACT=电压 mV，其余=0）
 * @param value1  事件关联值 1（CONTRACT=电流 mA，其余=0）
 * @param context  用户上下文
 */
typedef void (*usbpd_dpm_event_fn)(uint8_t port, uint8_t event, uint32_t value0, uint32_t value1, void *context);

/**
 * @brief  Source 模式板级电源控制回调（仅 Source 角色使用）
 *
 * @param port  端口号
 * @param enable  1=开启输出，0=关闭输出
 * @param voltage_mv  目标电压（mV）
 * @param current_ma  目标电流（mA）
 * @param context  用户上下文
 * @return USBPD_OK 控制成功；USBPD_ERR 控制失败
 */
typedef int (*usbpd_dpm_set_source_fn)(uint8_t port, uint8_t enable, uint32_t voltage_mv, uint32_t current_ma,
                                       void *context);

/**
 * @brief  Source 模式电源就绪检查回调
 *
 * @param port  端口号
 * @param voltage_mv  待检查电压（mV）
 * @param context  用户上下文
 * @return 1=指定电压已就绪，0=未就绪
 */
typedef uint8_t (*usbpd_dpm_source_ready_fn)(uint8_t port, uint32_t voltage_mv, void *context);

/**
 * @brief  通用开关类回调（VCONN 供电、VBUS 放电等）
 *
 * @param port  端口号
 * @param enable  1=开启，0=关闭
 * @param context  用户上下文
 * @return USBPD_OK 控制成功；USBPD_ERR 控制失败
 */
typedef int (*usbpd_dpm_set_switch_fn)(uint8_t port, uint8_t enable, void *context);

/** @brief DPM 回调集合（由应用层填充，传入 pd_sink_init / pd_source_init 等策略层） */
struct usbpd_dpm_callbacks_t
{
    usbpd_dpm_message_fn message_received;   /* 消息接收回调（可 NULL） */
    usbpd_dpm_get_extended_fn get_extended;   /* 扩展消息响应填充（可 NULL） */
    usbpd_dpm_event_fn event;                 /* 事件回调（可 NULL） */
    usbpd_dpm_set_source_fn set_source;       /* Source 电源输出控制（可 NULL） */
    usbpd_dpm_source_ready_fn source_ready;   /* Source 电源就绪检查（可 NULL） */
    usbpd_dpm_set_switch_fn set_vconn;        /* VCONN 开关（可 NULL，Cable/Plug 场景） */
    usbpd_dpm_set_switch_fn set_discharge;    /* VBUS 放电开关（可 NULL） */
    void *context;                            /* 回调用户上下文 */
};

#endif
