/**
* @file usbpd_frame.c
* @brief USB PD 底层帧校验与解码实现
*
* 所属模块：USB PD 协议栈（帧层，与 PHY 层交互的最底层协议处理）。
* 对应头文件：usbpd_frame.h。
*
* 本文件实现：
*   - usbpd_frame_wire_length_valid：基于报文头 data_objects 字段计算期望长度，
*     拒绝长度不符的线帧；额外拦截扩展消息携带 0 个数据对象的非法组合。
*   - usbpd_frame_decode_wire：将合法线帧的头、DO 拷贝到 usbpd_frame_t，
*     填充 payload_len（以 DO 个数而非字节数表示）与 raw_len。
*
* 注意：本模块不执行 CRC 校验，CRC 校验由 PHY 层在调用前完成。
*/
#include "usbpd_frame.h"
#include <string.h>

/* ===== 对外接口 ===== */
/**
 * @brief  校验 BMC 解码后原始线帧的长度合法性
 *
 * 合法长度 = 2 字节报文头 + data_objects×4 字节数据对象 + 4 字节 CRC；
 * 扩展消息（header.bits.extended=1）必须携带至少 1 个数据对象，以便容纳
 * 2 字节扩展消息头。
 *
 * @param wire  原始线帧字节（含头、DO、CRC）
 * @param wire_length  线帧总长度（字节）
 * @return 1=长度合法，0=长度非法或参数异常
 */
uint8_t usbpd_frame_wire_length_valid(const uint8_t *wire, uint8_t wire_length)
{
    union usbpd_header_u header;
    uint8_t expected_length;

    if ((wire == 0) || (wire_length < (2U + USBPD_CRC_LEN)) || (wire_length > USBPD_TRACE_FRAME_LEN))
    {
        return 0U;
    }

    header.bytes[0] = wire[0];
    header.bytes[1] = wire[1];
    expected_length = (uint8_t)(2U + (header.bits.data_objects * 4U) + USBPD_CRC_LEN);
    if (wire_length != expected_length)
    {
        return 0U;
    }

    /* Every Extended Message carries at least its two-byte Extended Header. */
    if ((header.bits.extended != 0U) && (header.bits.data_objects == 0U))
    {
        return 0U;
    }
    return 1U;
}

/**
 * @brief  将 CRC 校验通过的原始线帧解码为 usbpd_frame_t 结构
 *
 * 解码前会再次调用 usbpd_frame_wire_length_valid() 确认长度合法性。
 * 输出帧的 payload_len 字段以数据对象个数表示（与 usbpd_def.h 中
 * struct usbpd_frame_t 的字段语义一致），而非字节数。
 *
 * @param wire  CRC 校验通过的原始线帧字节（含头、DO、CRC）
 * @param wire_length  线帧总长度（字节）
 * @param sop  线帧所属 SOP 通道（仅 SOP/SOP'/SOP'' 合法）
 * @param frame  输出解码后的帧结构
 * @return USBPD_OK 解码成功；USBPD_ERR_PARAM 参数非法；USBPD_ERR_PROTOCOL 长度校验失败
 */
int usbpd_frame_decode_wire(const uint8_t *wire, uint8_t wire_length, uint8_t sop, struct usbpd_frame_t *frame)
{
    uint8_t payload_length;

    if ((wire == 0) || (frame == 0) || (sop > USBPD_SOP_DPRIME))
    {
        return USBPD_ERR_PARAM;
    }

    memset(frame, 0, sizeof(*frame));
    frame->sop = USBPD_SOP_INVALID;
    if (usbpd_frame_wire_length_valid(wire, wire_length) == 0U)
    {
        return USBPD_ERR_PROTOCOL;
    }

    frame->header.bytes[0] = wire[0];
    frame->header.bytes[1] = wire[1];
    payload_length = (uint8_t)(frame->header.bits.data_objects * 4U);
    if (payload_length != 0U)
    {
        memcpy(frame->payload, &wire[2], payload_length);
    }
    frame->payload_len = frame->header.bits.data_objects;
    frame->sop = sop;
    frame->raw_len = wire_length;
    return USBPD_OK;
}
