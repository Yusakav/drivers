/**
 * @file ld2410d_drv.c
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief HLK-LD2410D 人体存在雷达驱动实现 — UART通信、帧解析、配置命令、数据读取
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 */

#include "ld2410d_drv.h"
#include <string.h>
#include <math.h>

/* ==================== 内部常量 ==================== */

/** @brief 帧同步超时 (ms) */
#define SYNC_TIMEOUT_MS         100U

/** @brief 帧间字节超时 (ms) */
#define INTER_BYTE_TIMEOUT_MS   50U

/** @brief ACK 命令帧最小长度: 头(4) + 长度(2) + 命令字(2) + ACK状态(2) + 尾(4) */
#define ACK_FRAME_MIN_LEN       14U

/* ==================== 内部辅助函数 ==================== */

/**
 * @brief 从 buf 中读取 16 位小端无符号整数
 */
static inline uint16_t le16_to_u16 (const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

/**
 * @brief 从 buf 中读取 32 位小端无符号整数
 */
static inline uint32_t le32_to_u32 (const uint8_t *buf)
{
    return (uint32_t)buf[0]
           | ((uint32_t)buf[1] << 8)
           | ((uint32_t)buf[2] << 16)
           | ((uint32_t)buf[3] << 24);
}

/**
 * @brief 将 16 位值写入 buf (小端)
 */
static inline void u16_to_le16 (uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
}

/**
 * @brief 将 32 位值写入 buf (小端)
 */
static inline void u32_to_le32 (uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
}

/**
 * @brief 构建命令帧并发送
 *
 * 帧格式: FD FC FB FA | LEN(2B, 含命令字+数据) | CMD(2B) | DATA(N B) | 04 03 02 01
 *
 * @param dev       设备句柄
 * @param cmd_word  命令字 (2 字节)
 * @param data      命令数据 (可为 NULL)
 * @param data_len  数据长度
 */
static void send_command (struct ld2410d_dev_t *dev,
                           uint16_t cmd_word,
                           const uint8_t *data,
                           uint16_t data_len)
{
    uint8_t *p = dev->tx_buf;
    uint16_t frame_data_len = 2 + data_len;  /* 命令字(2) + 数据(N) */

    /* 帧头 */
    *p++ = LD2410D_FRAME_HEADER_0;
    *p++ = LD2410D_FRAME_HEADER_1;
    *p++ = LD2410D_FRAME_HEADER_2;
    *p++ = LD2410D_FRAME_HEADER_3;

    /* 帧内数据长度 (小端) */
    u16_to_le16 (p, frame_data_len);
    p += 2;

    /* 命令字 (小端) */
    u16_to_le16 (p, cmd_word);
    p += 2;

    /* 命令数据 */
    if (data && data_len) {
        memcpy (p, data, data_len);
        p += data_len;
    }

    /* 帧尾 */
    *p++ = LD2410D_FRAME_FOOTER_0;
    *p++ = LD2410D_FRAME_FOOTER_1;
    *p++ = LD2410D_FRAME_FOOTER_2;
    *p++ = LD2410D_FRAME_FOOTER_3;

    dev->uart.send (dev->tx_buf, (uint16_t)(p - dev->tx_buf));
}

/**
 * @brief 接收 ACK 帧并校验
 *
 * ACK 帧格式: FD FC FB FA | LEN(2B) | CMD(2B) | ACK_STAT(2B) | RET(N B) | 04 03 02 01
 *
 * @param dev        设备句柄
 * @param cmd_word   期望的命令字 (用于校验)
 * @param ret_data   返回值数据输出缓冲区 (可为 NULL)
 * @param ret_len    期望的返回值长度 (可为 0)
 * @param timeout_ms 超时 (ms)
 * @return LD2410D_OK 成功, 其他为错误码
 */
static int8_t recv_ack (struct ld2410d_dev_t *dev,
                         uint16_t cmd_word,
                         uint8_t *ret_data,
                         uint16_t ret_len,
                         uint32_t timeout_ms)
{
    uint16_t total;
    uint16_t frame_data_len;
    uint16_t rxd_cmd;
    uint16_t ack_status;
    const uint8_t *p;

    /* 先接收最小长度, 再根据帧内数据长度字段决定是否需要续收 */
    total = dev->uart.recv (dev->rx_buf, ACK_FRAME_MIN_LEN, timeout_ms);
    if (total < ACK_FRAME_MIN_LEN) {
        return LD2410D_ERR_TIMEOUT;
    }

    /* 校验帧头 */
    if (dev->rx_buf[0] != LD2410D_FRAME_HEADER_0 ||
        dev->rx_buf[1] != LD2410D_FRAME_HEADER_1 ||
        dev->rx_buf[2] != LD2410D_FRAME_HEADER_2 ||
        dev->rx_buf[3] != LD2410D_FRAME_HEADER_3) {
        return LD2410D_ERR_FRAME;
    }

    /* 帧内数据长度 */
    frame_data_len = le16_to_u16 (&dev->rx_buf[4]);

    /* 如果帧内数据长度大于最小帧中剩余部分, 继续接收 */
    if (frame_data_len > (ACK_FRAME_MIN_LEN - 10)) {
        uint16_t remaining = frame_data_len - (ACK_FRAME_MIN_LEN - 10);
        uint16_t rxd = dev->uart.recv (dev->rx_buf + ACK_FRAME_MIN_LEN,
                                        remaining, INTER_BYTE_TIMEOUT_MS);
        total += rxd;
    }

    /* 校验帧尾: 帧尾位于 4(头) + 2(长度) + frame_data_len 处 */
    p = dev->rx_buf + 4 + 2 + frame_data_len;
    if (total < (6U + frame_data_len + 4U)) {
        return LD2410D_ERR_FRAME;
    }
    if (p[0] != LD2410D_FRAME_FOOTER_0 ||
        p[1] != LD2410D_FRAME_FOOTER_1 ||
        p[2] != LD2410D_FRAME_FOOTER_2 ||
        p[3] != LD2410D_FRAME_FOOTER_3) {
        return LD2410D_ERR_FRAME;
    }

    /* 校验命令字 (位于偏移 6) */
    rxd_cmd = le16_to_u16 (&dev->rx_buf[6]);
    if (rxd_cmd != cmd_word) {
        return LD2410D_ERR_FRAME;
    }

    /* 校验 ACK 状态 (位于偏移 8) */
    ack_status = le16_to_u16 (&dev->rx_buf[8]);
    if (ack_status != 0x0000) {
        return LD2410D_ERR_ACK;
    }

    /* 提取返回值 */
    if (ret_data && ret_len) {
        uint16_t copy_len = (frame_data_len - 4) < ret_len
                            ? (frame_data_len - 4) : ret_len;
        memcpy (ret_data, &dev->rx_buf[10], copy_len);
    }

    dev->rx_len = total;
    return LD2410D_OK;
}

/**
 * @brief 发送无数据的命令并等待 ACK
 */
static int8_t send_cmd_no_data (struct ld2410d_dev_t *dev,
                                 uint16_t cmd_word,
                                 uint8_t *ret_data,
                                 uint16_t ret_len,
                                 uint32_t timeout_ms)
{
    send_command (dev, cmd_word, NULL, 0);
    return recv_ack (dev, cmd_word, ret_data, ret_len, timeout_ms);
}

/* ==================== 公开 API 实现 ==================== */

/**
 * @brief 初始化 LD2410D 设备句柄
 */
void ld2410d_init (struct ld2410d_dev_t *dev,
                   const struct ld2410d_uart_ops_t *ops)
{
    memset (dev, 0, sizeof (*dev));
    if (ops) {
        dev->uart = *ops;
    }
    dev->output_mode = 0;
}

/**
 * @brief 使能配置模式
 */
int8_t ld2410d_enable_config (struct ld2410d_dev_t *dev)
{
    uint8_t cmd_data[2];

    /* 命令值: 0x0001 (小端) */
    u16_to_le16 (cmd_data, 0x0001);

    send_command (dev, LD2410D_CMD_ENABLE_CFG, cmd_data, 2);
    return recv_ack (dev, LD2410D_CMD_ENABLE_CFG, NULL, 0, 200);
}

/**
 * @brief 结束配置模式
 */
int8_t ld2410d_end_config (struct ld2410d_dev_t *dev)
{
    return send_cmd_no_data (dev, LD2410D_CMD_END_CFG, NULL, 0, 200);
}

/**
 * @brief 读取固件版本
 */
int8_t ld2410d_read_firmware_version (struct ld2410d_dev_t *dev,
                                       char *ver, uint8_t *ver_len)
{
    int8_t ret;
    uint8_t ret_buf[32];
    uint8_t vlen;

    memset (ret_buf, 0, sizeof (ret_buf));

    ret = send_cmd_no_data (dev, LD2410D_CMD_READ_VERSION,
                            ret_buf, sizeof (ret_buf), 200);
    if (ret != LD2410D_OK) {
        return ret;
    }

    /* 返回值格式: 版本号长度 (2 字节 LE) + 版本号字节串 */
    vlen = le16_to_u16 (ret_buf);
    if (vlen > 30) {
        vlen = 30;
    }

    if (ver) {
        memcpy (ver, ret_buf + 2, vlen);
        ver[vlen] = '\0';
    }
    if (ver_len) {
        *ver_len = vlen;
    }

    return LD2410D_OK;
}

/**
 * @brief 读取序列号 (十六进制)
 */
int8_t ld2410d_read_sn_hex (struct ld2410d_dev_t *dev, uint8_t *sn)
{
    int8_t ret;
    uint8_t ret_buf[16];

    memset (ret_buf, 0, sizeof (ret_buf));

    ret = send_cmd_no_data (dev, LD2410D_CMD_READ_SN_HEX,
                            ret_buf, sizeof (ret_buf), 200);
    if (ret != LD2410D_OK) {
        return ret;
    }

    /* 返回值格式: SN 长度 (2 字节 LE) + SN 字节串 */
    uint8_t sn_len = (uint8_t)le16_to_u16 (ret_buf);
    if (sn_len > 12) {
        sn_len = 12;
    }

    if (sn) {
        memcpy (sn, ret_buf + 2, sn_len);
    }

    return LD2410D_OK;
}

/**
 * @brief 读取序列号 (字符形式)
 */
int8_t ld2410d_read_sn_char (struct ld2410d_dev_t *dev,
                              char *sn_str, uint8_t *sn_len)
{
    int8_t ret;
    uint8_t ret_buf[32];

    memset (ret_buf, 0, sizeof (ret_buf));

    ret = send_cmd_no_data (dev, LD2410D_CMD_READ_SN_CHAR,
                            ret_buf, sizeof (ret_buf), 200);
    if (ret != LD2410D_OK) {
        return ret;
    }

    /* 返回值格式: SN 长度 (2 字节 LE) + SN 字节串 (ASCII) */
    uint8_t slen = (uint8_t)le16_to_u16 (ret_buf);
    if (slen > 30) {
        slen = 30;
    }

    if (sn_str) {
        memcpy (sn_str, ret_buf + 2, slen);
        sn_str[slen] = '\0';
    }
    if (sn_len) {
        *sn_len = slen;
    }

    return LD2410D_OK;
}

/**
 * @brief 读取单个传感器参数
 */
int8_t ld2410d_read_param (struct ld2410d_dev_t *dev,
                            uint16_t param_id, uint32_t *value)
{
    int8_t ret;
    uint8_t cmd_data[2];
    uint8_t ret_buf[4];

    /* 命令值: 参数 ID (2 字节 LE) */
    u16_to_le16 (cmd_data, param_id);

    send_command (dev, LD2410D_CMD_READ_PARAM, cmd_data, 2);
    ret = recv_ack (dev, LD2410D_CMD_READ_PARAM, ret_buf, 4, 200);
    if (ret != LD2410D_OK) {
        return ret;
    }

    if (value) {
        *value = le32_to_u32 (ret_buf);
    }

    return LD2410D_OK;
}

/**
 * @brief 写入单个传感器参数
 */
int8_t ld2410d_write_param (struct ld2410d_dev_t *dev,
                             uint16_t param_id, uint32_t value)
{
    uint8_t cmd_data[6];

    /* 命令值: 参数 ID (2 字节) + 参数值 (4 字节), 均小端 */
    u16_to_le16 (cmd_data, param_id);
    u32_to_le32 (cmd_data + 2, value);

    send_command (dev, LD2410D_CMD_WRITE_PARAM, cmd_data, 6);
    return recv_ack (dev, LD2410D_CMD_WRITE_PARAM, NULL, 0, 200);
}

/**
 * @brief 设置数据输出模式
 */
int8_t ld2410d_set_output_mode (struct ld2410d_dev_t *dev, uint32_t mode)
{
    uint8_t cmd_data[6];

    /* 命令值: 0x0000 (2 字节) + 模式值 (4 字节) */
    u16_to_le16 (cmd_data, 0x0000);
    u32_to_le32 (cmd_data + 2, mode);

    send_command (dev, LD2410D_CMD_SET_OUTPUT_MODE, cmd_data, 6);
    return recv_ack (dev, LD2410D_CMD_SET_OUTPUT_MODE, NULL, 0, 200);
}

/**
 * @brief 保存参数到 Flash
 */
int8_t ld2410d_save_params (struct ld2410d_dev_t *dev)
{
    return send_cmd_no_data (dev, LD2410D_CMD_SAVE_PARAM, NULL, 0, 200);
}

/**
 * @brief 上电自动增益调节
 */
int8_t ld2410d_auto_gain (struct ld2410d_dev_t *dev)
{
    int8_t ret;

    ret = send_cmd_no_data (dev, LD2410D_CMD_AUTO_GAIN, NULL, 0, 500);
    if (ret != LD2410D_OK) {
        return ret;
    }

    /* 调节完毕后雷达会主动上报一次 0xF000 ACK, 等待接收 */
    dev->uart.recv (dev->rx_buf, 64, 500);

    return LD2410D_OK;
}

/**
 * @brief 开始自动门限生成
 */
int8_t ld2410d_start_auto_threshold (struct ld2410d_dev_t *dev,
                                      uint16_t trigger_coeff,
                                      uint16_t hold_coeff,
                                      uint16_t static_coeff)
{
    uint8_t cmd_data[6];

    /* 命令值: 触发系数(2 字节) + 保持系数(2 字节) + 微动系数(2 字节) */
    u16_to_le16 (cmd_data, trigger_coeff);
    u16_to_le16 (cmd_data + 2, hold_coeff);
    u16_to_le16 (cmd_data + 4, static_coeff);

    send_command (dev, LD2410D_CMD_AUTO_THRESHOLD, cmd_data, 6);
    return recv_ack (dev, LD2410D_CMD_AUTO_THRESHOLD, NULL, 0, 200);
}

/**
 * @brief 查询自动门限生成进度
 */
int8_t ld2410d_query_threshold_progress (struct ld2410d_dev_t *dev,
                                          uint8_t *progress)
{
    int8_t ret;
    uint8_t ret_buf[2];

    ret = send_cmd_no_data (dev, LD2410D_CMD_QUERY_THRESHOLD,
                            ret_buf, 2, 200);
    if (ret != LD2410D_OK) {
        return ret;
    }

    if (progress) {
        /* 返回值: 百分比 (2 字节 LE) */
        *progress = (uint8_t)le16_to_u16 (ret_buf);
    }

    return LD2410D_OK;
}

/**
 * @brief 读取基本配置并缓存
 */
int8_t ld2410d_read_basic_config (struct ld2410d_dev_t *dev)
{
    int8_t ret;
    uint32_t val;

    /* 最大距离 */
    ret = ld2410d_read_param (dev, LD2410D_PARAM_MAX_DISTANCE, &val);
    if (ret != LD2410D_OK) {
        return ret;
    }
    dev->basic_cfg.max_distance = (uint8_t)val;

    /* 目标消失延迟 */
    ret = ld2410d_read_param (dev, LD2410D_PARAM_DISAPPEAR_DELAY, &val);
    if (ret != LD2410D_OK) {
        return ret;
    }
    dev->basic_cfg.disappear_delay = (uint16_t)val;

    return LD2410D_OK;
}

/**
 * @brief 将缓存的配置写入传感器
 */
int8_t ld2410d_write_basic_config (struct ld2410d_dev_t *dev)
{
    int8_t ret;

    /* 最大距离 */
    ret = ld2410d_write_param (dev,
                                LD2410D_PARAM_MAX_DISTANCE,
                                dev->basic_cfg.max_distance);
    if (ret != LD2410D_OK) {
        return ret;
    }

    /* 目标消失延迟 */
    ret = ld2410d_write_param (dev,
                                LD2410D_PARAM_DISAPPEAR_DELAY,
                                dev->basic_cfg.disappear_delay);
    if (ret != LD2410D_OK) {
        return ret;
    }

    return LD2410D_OK;
}

/**
 * @brief 解析工程模式数据帧
 */
int8_t ld2410d_parse_engineering_frame (struct ld2410d_dev_t *dev,
                                         ld2410d_engineering_data_t *data)
{
    const uint8_t *buf = dev->rx_buf;
    uint16_t buf_len = dev->rx_len;
    uint16_t frame_data_len;
    uint8_t  status;
    uint16_t distance;
    int      i;
    const uint8_t *p;

    /* 最小长度: 头(4) + 长度(2) + 结果(1) + 距离(2) + 能量(128) + 尾(4) = 141 */
    if (buf_len < 141U) {
        return LD2410D_ERR_FRAME;
    }

    /* 帧头校验 */
    if (buf[0] != LD2410D_DATA_HEADER_0 ||
        buf[1] != LD2410D_DATA_HEADER_1 ||
        buf[2] != LD2410D_DATA_HEADER_2 ||
        buf[3] != LD2410D_DATA_HEADER_3) {
        return LD2410D_ERR_FRAME;
    }

    /* 帧内数据长度 (不包含头和尾) */
    frame_data_len = le16_to_u16 (&buf[4]);

    /* 帧尾校验 */
    p = buf + 4 + 2 + frame_data_len;
    if ((uint16_t)(p - buf + 4) > buf_len) {
        return LD2410D_ERR_FRAME;
    }
    if (p[0] != LD2410D_DATA_FOOTER_0 ||
        p[1] != LD2410D_DATA_FOOTER_1 ||
        p[2] != LD2410D_DATA_FOOTER_2 ||
        p[3] != LD2410D_DATA_FOOTER_3) {
        return LD2410D_ERR_FRAME;
    }

    /* 检测结果 (偏移 6) */
    status = buf[6];
    if (status > 0x02) {
        status = 0x00;
    }

    /* 目标距离 (偏移 7, 2 字节 LE, cm) */
    distance = le16_to_u16 (&buf[7]);

    if (data) {
        data->status   = (enum ld2410d_detect_status_e)status;
        data->distance = distance;

        /* 能量值: 偏移 9 开始, 128 字节 = 32 个距离门 * 4 字节
         *   前 64 字节: 16 个距离门运动能量
         *   后 64 字节: 16 个距离门微动 & 静止能量
         */
        p = &buf[9];
        for (i = 0; i < 16; i++) {
            data->gates[i].motion_energy = le32_to_u32 (p);
            p += 4;
        }
        for (i = 0; i < 16; i++) {
            data->gates[i].static_energy = le32_to_u32 (p);
            p += 4;
        }
    }

    return LD2410D_OK;
}

/**
 * @brief 等待并接收一帧工程模式数据
 */
int8_t ld2410d_recv_engineering_frame (struct ld2410d_dev_t *dev,
                                        ld2410d_engineering_data_t *data,
                                        uint32_t timeout)
{
    uint8_t  sync_byte;
    uint16_t rx_total;
    uint32_t elapsed = 0;
    uint16_t frame_data_len;
    uint16_t remaining;
    uint16_t rxd;

    /* 清空缓冲区 */
    dev->uart.flush ();

    /* 同步帧头: 逐字节接收直到匹配 F4 */
    while (elapsed < timeout) {
        rxd = dev->uart.recv (&sync_byte, 1, 10);
        if (rxd != 1) {
            elapsed += 10;
            continue;
        }

        if (sync_byte == LD2410D_DATA_HEADER_0) {
            dev->rx_buf[0] = sync_byte;
            break;
        }

        elapsed += 10;
    }

    if (elapsed >= timeout) {
        return LD2410D_ERR_TIMEOUT;
    }

    /* 接收剩余帧头 (F3 F2 F1) */
    rxd = dev->uart.recv (dev->rx_buf + 1, 3, INTER_BYTE_TIMEOUT_MS);
    if (rxd != 3) {
        return LD2410D_ERR_TIMEOUT;
    }
    if (dev->rx_buf[1] != LD2410D_DATA_HEADER_1 ||
        dev->rx_buf[2] != LD2410D_DATA_HEADER_2 ||
        dev->rx_buf[3] != LD2410D_DATA_HEADER_3) {
        return LD2410D_ERR_FRAME;
    }

    /* 接收长度字段 (2 字节) */
    rxd = dev->uart.recv (dev->rx_buf + 4, 2, INTER_BYTE_TIMEOUT_MS);
    if (rxd != 2) {
        return LD2410D_ERR_TIMEOUT;
    }

    frame_data_len = le16_to_u16 (&dev->rx_buf[4]);

    /* 最小长度检查: 结果(1) + 距离(2) + 能量(128) = 131 */
    if (frame_data_len < 131U) {
        return LD2410D_ERR_FRAME;
    }

    /* 接收帧内数据 */
    rx_total = 6;
    remaining = frame_data_len;
    while (remaining > 0) {
        rxd = dev->uart.recv (dev->rx_buf + rx_total, remaining,
                               INTER_BYTE_TIMEOUT_MS);
        if (rxd == 0) {
            return LD2410D_ERR_TIMEOUT;
        }
        rx_total += rxd;
        remaining -= rxd;
    }

    /* 接收帧尾 (4 字节) */
    rxd = dev->uart.recv (dev->rx_buf + rx_total, 4, INTER_BYTE_TIMEOUT_MS);
    if (rxd != 4) {
        return LD2410D_ERR_TIMEOUT;
    }
    rx_total += 4;

    /* 校验帧尾 */
    if (dev->rx_buf[rx_total - 4] != LD2410D_DATA_FOOTER_0 ||
        dev->rx_buf[rx_total - 3] != LD2410D_DATA_FOOTER_1 ||
        dev->rx_buf[rx_total - 2] != LD2410D_DATA_FOOTER_2 ||
        dev->rx_buf[rx_total - 1] != LD2410D_DATA_FOOTER_3) {
        return LD2410D_ERR_FRAME;
    }

    dev->rx_len = rx_total;

    /* 解析 */
    return ld2410d_parse_engineering_frame (dev, data);
}

/**
 * @brief 将能量原始值 M 转换为 dB 值 (单位 0.01 dB)
 */
uint16_t ld2410d_energy_to_db (uint32_t raw_value)
{
    double db_val;

    if (raw_value == 0) {
        return 0;
    }

    /* N = 10 * log10(M), 结果放大 100 倍以保留两位小数 */
    db_val = 10.0 * log10 ((double)raw_value);
    return (uint16_t)(db_val * 100.0);
}
