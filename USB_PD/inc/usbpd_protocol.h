/**
* @file usbpd_protocol.h
* @brief USB Power Delivery（USB PD）协议层对外接口声明
*
* 所属模块：USB PD 协议栈（驱动层，与具体控制器/平台无关）。
* 对应实现：usbpd_protocol.c。
*
* 协议层职责：
*   - 构造/解析报文头，维护各 SOP 通道 MessageID 收发序号与去重；
*   - 发送后等待 GoodCRC，超时按 USBPD_N_RETRY_COUNT 重发并上报事件；
*   - 扩展消息自动分块发送/接收（Chunking），对端请求下一分块；
*   - 处理 Soft Reset / Hard Reset / 接收溢出等复位流程。
*
* 调用约定：
*   1. 先调用 usbpd_protocol_init() 注册事件回调；
*   2. 再调用 usbpd_protocol_configure() 配置版本与角色；
*   3. 主循环以毫秒节拍周期调用 usbpd_protocol_task()；
*   4. 通过 usbpd_protocol_send_* 系列接口发送报文，事件经回调上报。
*/
#ifndef USBPD_PROTOCOL_H
#define USBPD_PROTOCOL_H

#include "usbpd_def.h"

/** @brief 协议层事件类型（经回调上报上层，见 usbpd_protocol_handler_t） */
enum usbpd_protocol_event_e
{
    USBPD_PROTOCOL_RX = 0U,      /* 接收到普通/扩展消息（完整重组后上报） */
    USBPD_PROTOCOL_TX_GOODCRC,  /* 本端发送帧已被对端 GoodCRC 确认 */
    USBPD_PROTOCOL_TX_TIMEOUT,  /* 本端发送帧 GoodCRC 超时（已用尽重试次数） */
    USBPD_PROTOCOL_SOFT_RESET,  /* 收到对端 Soft Reset 控制消息 */
    USBPD_PROTOCOL_HARD_RESET,  /* 收到对端 Hard Reset 信号 */
    USBPD_PROTOCOL_EXT_RX,      /* 扩展消息分块重组完成 */
    USBPD_PROTOCOL_RX_OVERFLOW, /* PHY 层接收缓冲溢出，协议层已复位 */
    USBPD_PROTOCOL_ERROR,       /* 通用协议错误（帧格式非法等） */
};

/** @brief 协议层事件回调携带的消息内容 */
struct usbpd_protocol_msg_t
{
    union usbpd_header_u header;       /* 报文头 */
    const uint8_t *payload;            /* 负载数据（扩展消息为重组后的完整数据） */
    uint16_t length;                   /* 负载长度（字节） */
    uint8_t sop;                       /* SOP 类型（见 usbpd_sop_e） */
};

/** @brief 协议层事件回调函数类型
 *
 *  @param event  事件类型（见 usbpd_protocol_event_e）
 *  @param msg  事件关联报文（部分事件可为 NULL）
 *  @param arg  用户参数（由 usbpd_protocol_init 传入）
 */
typedef void (*usbpd_protocol_handler_t)(uint8_t event, const struct usbpd_protocol_msg_t *msg, void *arg);

/**
 * @brief  配置协议层版本与角色，并同步设置 PHY 层角色
 *
 * @param revision  协议版本（见 usbpd_revision_e，大于 Rev3.0 按 Rev3.0 处理）
 * @param power_role  电源角色（0=Sink，非 0=Source）
 * @param data_role  数据角色（0=UFP，非 0=DFP）
 */
void usbpd_protocol_configure(uint8_t revision, uint8_t power_role, uint8_t data_role);

/**
 * @brief  发送控制消息（无数据对象）
 *
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param type  控制消息类型（见 usbpd_ctrl_e）
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 发起成功；USBPD_ERR_PARAM 参数非法；USBPD_BUSY 上一帧尚未被确认
 */
int usbpd_protocol_send_ctrl(uint8_t sop, uint8_t type, uint32_t now_ms);

/**
 * @brief  发送数据消息（携带数据对象）
 *
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param type  数据消息类型（见 usbpd_data_e）
 * @param objects  数据对象数组（小端序 32 位字）
 * @param count  数据对象数量（1..USBPD_MAX_DATA_OBJ）
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 发起成功；USBPD_ERR_PARAM 参数非法；USBPD_BUSY 上一帧尚未被确认
 */
int usbpd_protocol_send_data(uint8_t sop, uint8_t type, const uint32_t *objects, uint8_t count, uint32_t now_ms);

/**
 * @brief  通过 SOP 通道发送扩展消息（自动分块）
 *
 * @param type  扩展消息类型（见 usbpd_extended_e）
 * @param data  扩展数据缓冲
 * @param length  数据长度（1..USBPD_EXT_DATA_MAX 字节）
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 首块发起成功；USBPD_ERR_PARAM 参数非法；USBPD_BUSY 发送未就绪
 */
int usbpd_protocol_send_extended(uint8_t type, const uint8_t *data, uint16_t length, uint32_t now_ms);

/**
 * @brief  通过指定 SOP 通道发送扩展消息（自动分块）
 *
 * 长度超过 USBPD_EXT_CHUNK_DATA_MAX 时自动分块，每块发出后等待对端
 * Chunk Request 再继续发送下一块。
 *
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param type  扩展消息类型（见 usbpd_extended_e）
 * @param data  扩展数据缓冲
 * @param length  数据长度（1..USBPD_EXT_DATA_MAX 字节）
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 首块发起成功；USBPD_ERR_PARAM 参数非法；USBPD_BUSY 发送未就绪
 */
int usbpd_protocol_send_extended_sop(uint8_t sop, uint8_t type, const uint8_t *data, uint16_t length, uint32_t now_ms);

/**
 * @brief  发送 Extended Control 消息（两字节扩展控制数据块）
 *
 * @param subtype  扩展控制类型（见 usbpd_extended_control_e）
 * @param data  与类型相关的数据字节
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 首块发起成功；USBPD_ERR_PARAM 参数非法；USBPD_BUSY 发送未就绪
 */
int usbpd_protocol_send_extended_control(uint8_t subtype, uint8_t data, uint32_t now_ms);

/**
 * @brief  发送 Hard Reset 信号（经 PHY 层发送，不等 GoodCRC）
 *
 * 发送前放弃当前待确认帧。
 *
 * @param now_ms  当前系统时间（毫秒）
 * @return USBPD_OK 发起成功；USBPD_BUSY PHY 忙
 */
int usbpd_protocol_send_hard_reset(uint32_t now_ms);

/**
 * @brief  复位协议层运行状态
 *
 * 保留事件回调与版本/角色配置，清空 MessageID、收发缓冲与全部扩展消息状态。
 */
void usbpd_protocol_reset(void);

/**
 * @brief  初始化协议层，注册事件回调并复位全部状态
 *
 * 默认协议版本为 Rev3.0，角色需另行调用 usbpd_protocol_configure() 配置。
 *
 * @param handler  协议事件回调（事件类型见 usbpd_protocol_event_e）
 * @param arg  回调用户参数
 */
void usbpd_protocol_init(usbpd_protocol_handler_t handler, void *arg);

/**
 * @brief  协议层周期任务（需在主循环中以毫秒节拍调用）
 *
 * 依次处理：接收溢出恢复、帧接收分发、GoodCRC 超时重发（超过
 * USBPD_N_RETRY_COUNT 上报 USBPD_PROTOCOL_TX_TIMEOUT）、扩展消息分块
 * 收发调度与分块超时处理。
 *
 * @param now_ms  当前系统时间（毫秒）
 */
void usbpd_protocol_task(uint32_t now_ms);

#endif
