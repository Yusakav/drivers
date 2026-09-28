/**
* @file usbpd_frame.h
* @brief USB PD 底层帧校验与解码接口声明
*
* 所属模块：USB PD 协议栈（帧层，与 PHY 层交互的最底层协议处理）。
* 对应实现：usbpd_frame.c。
*
* 帧层职责：
*   - 校验 PHY 层 BMC 解码后原始线帧的长度合法性（2 字节头 + N×4 字节 DO + 4 字节 CRC）；
*   - 将合法线帧解码为协议栈内部统一的 usbpd_frame_t 结构，供协议层消费。
*
* 调用约定：PHY 层完成 BMC 解码与 CRC 校验后，先调用
* usbpd_frame_wire_length_valid() 判定长度，再调用 usbpd_frame_decode_wire() 解码。
*/
#ifndef USBPD_FRAME_H
#define USBPD_FRAME_H

#include "usbpd_def.h"

/**
 * @brief  校验 BMC 解码后原始线帧的长度合法性
 *
 * 合法长度 = 2 字节报文头 + data_objects×4 字节数据对象 + USBPD_CRC_LEN(4) 字节 CRC；
 * 扩展消息必须携带至少 1 个数据对象（否则无法放置 2 字节扩展消息头）。
 *
 * @param wire  原始线帧字节（含头、DO、CRC）
 * @param wire_length  线帧总长度（字节）
 * @return 1=长度合法，0=长度非法或参数异常
 */
uint8_t usbpd_frame_wire_length_valid(const uint8_t *wire, uint8_t wire_length);

/**
 * @brief  将 CRC 校验通过的原始线帧解码为 usbpd_frame_t 结构
 *
 * 解码前会再次调用 usbpd_frame_wire_length_valid() 确认长度合法性。
 *
 * @param wire  CRC 校验通过的原始线帧字节（含头、DO、CRC）
 * @param wire_length  线帧总长度（字节）
 * @param sop  线帧所属 SOP 通道（见 usbpd_sop_e，仅 SOP/SOP'/SOP'' 合法）
 * @param frame  输出解码后的帧结构（见 usbpd_frame_t）
 * @return USBPD_OK 解码成功；USBPD_ERR_PARAM 参数非法；USBPD_ERR_PROTOCOL 长度校验失败
 */
int usbpd_frame_decode_wire(const uint8_t *wire, uint8_t wire_length, uint8_t sop, struct usbpd_frame_t *frame);

#endif
