/**
* @file usbpd_message.h
* @brief USB PD 报文观察器（Observer）对外接口声明
*
* 所属模块：USB PD 协议栈（调试辅助层，与具体控制器/平台无关）。
* 对应实现：usbpd_message.c。
*
* 观察器职责：
*   - 将物理层收发的原始帧记录到环形缓冲（底层基于 message_buffer_t）；
*   - 支持 GoodCRC / 错误 / 复位等标志位的附带记录；
*   - 提供格式化输出接口 usbpd_observer_service()，将缓冲内记录解码为
*     人类可读的日志行（包含消息类型名、SOP、MessageID、PDO/RDO 概要等）。
*
* 使用约定：
*   1. 先调用 usbpd_observer_init() 初始化环形缓冲；
*   2. 在 PHY 层收/发完成后调用 usbpd_observer_record() 登记原始帧；
*   3. 主循环周期性调用 usbpd_observer_service() 刷写日志。
*/
#ifndef USBPD_MESSAGE_H
#define USBPD_MESSAGE_H

#include "usbpd_def.h"
#include "message_buffer.h"

/** @brief 观察器记录标志位（可按位 OR 组合） */
enum usbpd_observer_flag_e
{
    USBPD_OBSERVER_GOODCRC = 1U, /* 伴随 GoodCRC 确认的帧 */
    USBPD_OBSERVER_ERROR   = 2U, /* 帧格式/长度异常 */
    USBPD_OBSERVER_RESET   = 4U, /* Hard/Soft Reset 事件 */
};

/** @brief 观察器日志输出回调函数类型
 *
 *  @param text  单行日志文本（非 NUL 终止字符串）
 *  @param arg  用户参数（由 usbpd_observer_service 传入）
 */
typedef void (*usbpd_observer_write_fn)(const char *text, void *arg);

/**
 * @brief  向观察器登记一条原始帧记录（自动存入环形缓冲）
 *
 * @param direction  传输方向（MESSAGE_BUFFER_TX=发送，MESSAGE_BUFFER_RX=接收）
 * @param sop  SOP 类型（见 usbpd_sop_e）
 * @param raw  原始帧字节（可为 NULL，但 length 须为 0）
 * @param length  原始帧长度（字节，超过 USBPD_MESSAGE_BUFFER_PAYLOAD_MAX 将被截断并置 ERROR 标志）
 * @param timestamp_ms  记录时刻（毫秒，通常为当前系统时间）
 * @param flags  标志位（见 usbpd_observer_flag_e，可按位 OR 组合）
 */
void usbpd_observer_record(uint8_t direction, uint8_t sop, const uint8_t *raw, uint8_t length, uint32_t timestamp_ms,
                           uint8_t flags);

/**
 * @brief  从观察器环形缓冲中弹出一条记录
 *
 * @param record  输出记录结构（见 message_buffer_record_t）
 * @return 1=成功弹出，0=缓冲为空
 */
uint8_t usbpd_observer_pop(struct message_buffer_record_t *record);

/** @brief  清空观察器环形缓冲 */
void usbpd_observer_clear(void);

/**
 * @brief  查询观察器当前待处理记录数
 * @return 待处理记录数（0..USBPD_MESSAGE_BUFFER_DEPTH）
 */
uint16_t usbpd_observer_count(void);

/**
 * @brief  查询观察器自初始化以来累计丢弃记录数
 * @return 丢弃记录总数（环形缓冲满时 push 失败累计）
 */
uint32_t usbpd_observer_dropped(void);

/**
 * @brief  格式化并刷写观察器记录到回调输出
 *
 * 每次最多处理 max_records 条，输出格式示例：
 *   [USBPD][12345][TX][SOP][GOODCRC] id=3 ndo=0 rev=2
 *   [USBPD][12346][RX][SOP][SOURCE_CAP] id=0 ndo=1 rev=2 pdo1=fixed:5000mV/900mA ...
 *
 * @param max_records  本次最多处理记录数（0=不处理直接返回）
 * @param write  输出回调（可为 NULL，此时仅消费记录不输出）
 * @param arg  回调用户参数
 */
void usbpd_observer_service(uint8_t max_records, usbpd_observer_write_fn write, void *arg);

/** @brief  初始化观察器环形缓冲（须在使用其他接口前调用） */
void usbpd_observer_init(void);

#endif
