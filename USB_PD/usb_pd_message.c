/**
 * @file usb_pd_message.c
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief USB PD 消息队列管理 — 环形缓冲区实现
 * @version 0.1
 * @date 2025-12-21
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "usb_pd_message.h"
#include <string.h>
#include <stdio.h>

#define LOG_LEVEL LOG_LVL_DEBUG
#define LOG_TAG  "PD MSG"
#include "logger.h"

/**
 * @brief 全局消息队列实例
 */
static struct usb_pd_message_handle_t msg_queue;

/**
 * @brief USB PD 消息队列重置
 *
 * 清零读/写指针和消息计数，复位消息队列为空状态。
 */
void usb_pd_reset_message(void)
{
    memset(&msg_queue, 0, sizeof(msg_queue));
    LOG_D("Message queue reset");
}

/**
 * @brief USB PD 消息存储（环形缓冲区写入）
 *
 * 将一条 PD 消息存入环形缓冲区。当缓冲区满时，覆盖最旧的消息。
 *
 * @param type 消息 SOP 类型 (USBPD_SOP_TYPE_SOP / SOP' / SOP'' 等)
 * @param buf  消息原始数据缓冲区指针
 * @param len  消息数据长度（字节）
 */
void usb_pd_push_message(uint8_t type, uint8_t *buf, uint16_t len)
{
    struct usb_pd_message_t *msg;

    if (!buf || len == 0) {
        return;
    }

    /* 缓冲区满时覆盖最旧消息：推进读指针 */
    if (msg_queue.msg_count >= PD_MSG_BUFFER_SIZE) {
        msg_queue.read_index = (msg_queue.read_index + 1) % PD_MSG_BUFFER_SIZE;
        msg_queue.msg_count--;
    }

    msg = &msg_queue.mesg[msg_queue.write_index];
    msg->type = type;
    msg->len  = (len < PD_MSG_MAX_LEN) ? len : PD_MSG_MAX_LEN;
    memcpy(msg->buf, buf, msg->len);
    msg->id   = msg_queue.msg_count;
    msg->time = 0;
    msg->vbus_raw = 0;

    msg_queue.write_index = (msg_queue.write_index + 1) % PD_MSG_BUFFER_SIZE;
    msg_queue.msg_count++;

    LOG_D("Message pushed: type=0x%02X len=%d (total=%d)", type, len, msg_queue.msg_count);
}

/**
 * @brief 打印并移除消息队列中的所有消息
 *
 * 遍历队列中所有消息，打印每条消息的类型、长度、ID、时间戳和原始数据（十六进制），
 * 打印完成后调用 usb_pd_reset_message() 清空队列。
 */
void usb_pd_dump_and_clear_messages(void)
{
    uint16_t i, idx;

    LOG_I("=== PD Message Queue Dump (%d messages) ===", msg_queue.msg_count);

    for (i = 0; i < msg_queue.msg_count; i++) {
        idx = (msg_queue.read_index + i) % PD_MSG_BUFFER_SIZE;
        struct usb_pd_message_t *msg = &msg_queue.mesg[idx];

        LOG_I("[%d] type=0x%02X len=%d id=%d time=%lu vbus=%lu",
              i, msg->type, msg->len, msg->id,
              (unsigned long)msg->time, (unsigned long)msg->vbus_raw);

        /* 打印原始数据 */
        if (msg->len > 0) {
            printf("[%d] Raw:", i);
            for (uint16_t j = 0; j < msg->len; j++) {
                printf(" %02X", msg->buf[j]);
            }
            printf("\n");
        }
    }

    LOG_I("=== End of Message Queue ===");

    usb_pd_reset_message();
}
