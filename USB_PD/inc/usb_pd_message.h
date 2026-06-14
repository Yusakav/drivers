/**
 * @file usb_pd_message.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-12-21
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef USB_PD_MESSAGE_H
#define USB_PD_MESSAGE_H

#include "usb_pd_def.h"
#include <stdint.h>

#define PD_MSG_BUFFER_SIZE 16  // 消息缓冲区大小
#define PD_MSG_MAX_LEN 34      // 单条消息最大长度

/**
 * @brief PD 消息类型描述结构体
 */
struct pd_msg_type_desc {
    uint8_t type;     /**< 消息类型编号 */
    const char *name; /**< 消息类型名称 */
};

/**
 * @brief USB PD 消息结构体
 */
struct usb_pd_message_t {
    uint8_t buf[PD_MSG_MAX_LEN]; /**< 消息原始数据 */
    uint8_t len;                 /**< 消息原始数据长度 */
    uint8_t type;                /**< 消息类型(SOP/SOP'/SOP'') */
    uint16_t id;                 /**< 消息ID */
    uint32_t time;               /**< 接收时间戳 */
    uint32_t vbus_raw;           /**< VBUS电压 */
};

/**
 * @brief USB PD 消息队列管理器
 */
struct usb_pd_message_handle_t {
    struct usb_pd_message_t mesg[PD_MSG_BUFFER_SIZE]; /**< 消息缓冲区数组 */
    uint16_t read_index;                              /**< 读指针 */
    uint16_t write_index;                             /**< 写指针 */
    uint16_t msg_count;                               /**< 消息计数 */
};

/**
 * @brief USB PD 消息队列重置
 *
 */
void usb_pd_reset_message(void);

/**
 * @brief USB PD 消息存储
 *
 * @param type 消息类型类型
 * @param buf 接收的消息缓冲区
 * @param len 消息长度（字节）
 */
void usb_pd_push_message(uint8_t type, uint8_t *buf, uint16_t len);


/**
 * @brief 打印并移除消息队列中的所有消息
 */
void usb_pd_dump_and_clear_messages(void);


#endif /* USB_PD_MESSAGE_H */
