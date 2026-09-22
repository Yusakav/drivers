/**
 * @file ld2410d.c
 * @brief Platform-independent HLK-LD2410D radar driver.
 */

#include "ld2410d.h"
#include "ld2410d_protocol.h"
#include <math.h>
#include <string.h>

#define LD2410D_CMD_TIMEOUT_MS        200U
#define LD2410D_LONG_CMD_TIMEOUT_MS   500U
#define LD2410D_INTER_BYTE_TIMEOUT_MS  50U
#define LD2410D_SYNC_POLL_MS           10U

static ld2410d_ret_t validate_dev(const ld2410d_t *dev)
{
    if (dev == NULL) {
        return LD2410D_ERR_NULL;
    }
    if (dev->uart.send == NULL || dev->uart.recv == NULL ||
        dev->uart.flush == NULL) {
        return LD2410D_ERR_NULL;
    }
    return LD2410D_OK;
}

static ld2410d_ret_t uart_recv_exact(ld2410d_t *dev, uint8_t *buf,
                                      uint16_t len, uint32_t timeout_ms)
{
    uint16_t got_total = 0U;

    while (got_total < len) {
        uint16_t got = dev->uart.recv(dev->uart.user, buf + got_total,
                                      (uint16_t)(len - got_total),
                                      timeout_ms);
        if (got == 0U) {
            return LD2410D_ERR_TIMEOUT;
        }
        if (got > (uint16_t)(len - got_total)) {
            return LD2410D_ERR_IO;
        }
        got_total = (uint16_t)(got_total + got);
    }

    return LD2410D_OK;
}

static ld2410d_ret_t send_command(ld2410d_t *dev, uint16_t cmd,
                                   const uint8_t *payload,
                                   uint16_t payload_len)
{
    ld2410d_ret_t ret;
    uint16_t frame_len = 0U;

    ret = validate_dev(dev);
    if (ret != LD2410D_OK) {
        return ret;
    }

    ret = ld2410d_build_command_frame(dev->tx_buf, LD2410D_TX_BUF_MAX,
                                      cmd, payload, payload_len, &frame_len);
    if (ret != LD2410D_OK) {
        return ret;
    }

    dev->uart.send(dev->uart.user, dev->tx_buf, frame_len);
    return LD2410D_OK;
}

static ld2410d_ret_t recv_ack(ld2410d_t *dev, uint16_t expected_cmd,
                               uint8_t *payload, uint16_t payload_size,
                               uint16_t *payload_len, uint32_t timeout_ms)
{
    ld2410d_ret_t ret;
    uint16_t frame_data_len;
    uint32_t total_len;
    ld2410d_ack_frame_t ack;

    ret = validate_dev(dev);
    if (ret != LD2410D_OK) {
        return ret;
    }

    ret = uart_recv_exact(dev, dev->rx_buf, 6U, timeout_ms);
    if (ret != LD2410D_OK) {
        return ret;
    }

    frame_data_len = ld2410d_get_u16_le(&dev->rx_buf[4]);
    total_len = 6U + (uint32_t)frame_data_len + 4U;
    if (total_len > LD2410D_RX_BUF_MAX) {
        return LD2410D_ERR_OVERFLOW;
    }
    if (total_len < (LD2410D_FRAME_OVERHEAD + 4U)) {
        return LD2410D_ERR_FRAME;
    }

    ret = uart_recv_exact(dev, dev->rx_buf + 6U, (uint16_t)(total_len - 6U),
                          LD2410D_INTER_BYTE_TIMEOUT_MS);
    if (ret != LD2410D_OK) {
        return ret;
    }

    dev->rx_len = (uint16_t)total_len;
    ret = ld2410d_parse_ack_frame(dev->rx_buf, dev->rx_len, expected_cmd, &ack);
    if (ret != LD2410D_OK) {
        return ret;
    }

    if (payload_len != NULL) {
        *payload_len = ack.payload_len;
    }
    if (payload != NULL) {
        if (payload_size < ack.payload_len) {
            return LD2410D_ERR_OVERFLOW;
        }
        if (ack.payload_len > 0U) {
            memcpy(payload, ack.payload, ack.payload_len);
        }
    } else if (payload_size != 0U) {
        return LD2410D_ERR_NULL;
    }

    return LD2410D_OK;
}

static ld2410d_ret_t command_xfer(ld2410d_t *dev, uint16_t cmd,
                                   const uint8_t *cmd_payload,
                                   uint16_t cmd_payload_len,
                                   uint8_t *ack_payload,
                                   uint16_t ack_payload_size,
                                   uint16_t *ack_payload_len,
                                   uint32_t timeout_ms)
{
    ld2410d_ret_t ret = send_command(dev, cmd, cmd_payload, cmd_payload_len);
    if (ret != LD2410D_OK) {
        return ret;
    }
    return recv_ack(dev, cmd, ack_payload, ack_payload_size, ack_payload_len,
                    timeout_ms);
}

ld2410d_ret_t ld2410d_init(ld2410d_t *dev, const ld2410d_uart_ops_t *ops)
{
    if (dev == NULL || ops == NULL) {
        return LD2410D_ERR_NULL;
    }
    if (ops->send == NULL || ops->recv == NULL || ops->flush == NULL) {
        return LD2410D_ERR_NULL;
    }

    memset(dev, 0, sizeof(*dev));
    dev->uart = *ops;
    return LD2410D_OK;
}

ld2410d_ret_t ld2410d_enable_config(ld2410d_t *dev)
{
    uint8_t payload[2];

    ld2410d_put_u16_le(payload, 0x0001U);
    return command_xfer(dev, LD2410D_CMD_ENABLE_CFG, payload, sizeof(payload),
                        NULL, 0U, NULL, LD2410D_CMD_TIMEOUT_MS);
}

ld2410d_ret_t ld2410d_end_config(ld2410d_t *dev)
{
    return command_xfer(dev, LD2410D_CMD_END_CFG, NULL, 0U, NULL, 0U, NULL,
                        LD2410D_CMD_TIMEOUT_MS);
}

ld2410d_ret_t ld2410d_read_firmware_version(ld2410d_t *dev,
                                             char *version,
                                             uint8_t version_size,
                                             uint8_t *version_len)
{
    uint8_t payload[48];
    uint16_t payload_len = 0U;
    uint16_t reported_len;
    ld2410d_ret_t ret;

    if (version == NULL || version_len == NULL) {
        return LD2410D_ERR_NULL;
    }
    if (version_size == 0U) {
        return LD2410D_ERR_PARAM;
    }

    ret = command_xfer(dev, LD2410D_CMD_READ_VERSION, NULL, 0U, payload,
                       sizeof(payload), &payload_len, LD2410D_CMD_TIMEOUT_MS);
    if (ret != LD2410D_OK) {
        return ret;
    }
    if (payload_len < 2U) {
        return LD2410D_ERR_FRAME;
    }

    reported_len = ld2410d_get_u16_le(payload);
    if (reported_len > (uint16_t)(payload_len - 2U)) {
        return LD2410D_ERR_FRAME;
    }
    if (reported_len >= version_size) {
        return LD2410D_ERR_OVERFLOW;
    }

    memcpy(version, payload + 2U, reported_len);
    version[reported_len] = '\0';
    *version_len = (uint8_t)reported_len;
    return LD2410D_OK;
}

ld2410d_ret_t ld2410d_read_sn_hex(ld2410d_t *dev, uint8_t *sn,
                                   uint8_t sn_size, uint8_t *sn_len)
{
    uint8_t payload[32];
    uint16_t payload_len = 0U;
    uint16_t reported_len;
    ld2410d_ret_t ret;

    if (sn == NULL || sn_len == NULL) {
        return LD2410D_ERR_NULL;
    }

    ret = command_xfer(dev, LD2410D_CMD_READ_SN_HEX, NULL, 0U, payload,
                       sizeof(payload), &payload_len, LD2410D_CMD_TIMEOUT_MS);
    if (ret != LD2410D_OK) {
        return ret;
    }
    if (payload_len < 2U) {
        return LD2410D_ERR_FRAME;
    }

    reported_len = ld2410d_get_u16_le(payload);
    if (reported_len > (uint16_t)(payload_len - 2U)) {
        return LD2410D_ERR_FRAME;
    }
    if (reported_len > sn_size) {
        return LD2410D_ERR_OVERFLOW;
    }

    memcpy(sn, payload + 2U, reported_len);
    *sn_len = (uint8_t)reported_len;
    return LD2410D_OK;
}

ld2410d_ret_t ld2410d_read_sn_char(ld2410d_t *dev, char *sn,
                                    uint8_t sn_size, uint8_t *sn_len)
{
    uint8_t payload[48];
    uint16_t payload_len = 0U;
    uint16_t reported_len;
    ld2410d_ret_t ret;

    if (sn == NULL || sn_len == NULL) {
        return LD2410D_ERR_NULL;
    }
    if (sn_size == 0U) {
        return LD2410D_ERR_PARAM;
    }

    ret = command_xfer(dev, LD2410D_CMD_READ_SN_CHAR, NULL, 0U, payload,
                       sizeof(payload), &payload_len, LD2410D_CMD_TIMEOUT_MS);
    if (ret != LD2410D_OK) {
        return ret;
    }
    if (payload_len < 2U) {
        return LD2410D_ERR_FRAME;
    }

    reported_len = ld2410d_get_u16_le(payload);
    if (reported_len > (uint16_t)(payload_len - 2U)) {
        return LD2410D_ERR_FRAME;
    }
    if (reported_len >= sn_size) {
        return LD2410D_ERR_OVERFLOW;
    }

    memcpy(sn, payload + 2U, reported_len);
    sn[reported_len] = '\0';
    *sn_len = (uint8_t)reported_len;
    return LD2410D_OK;
}

ld2410d_ret_t ld2410d_read_param(ld2410d_t *dev, uint16_t param_id,
                                  uint32_t *value)
{
    uint8_t cmd_payload[2];
    uint8_t ack_payload[4];
    uint16_t ack_payload_len = 0U;
    ld2410d_ret_t ret;

    if (value == NULL) {
        return LD2410D_ERR_NULL;
    }

    ld2410d_put_u16_le(cmd_payload, param_id);
    ret = command_xfer(dev, LD2410D_CMD_READ_PARAM, cmd_payload,
                       sizeof(cmd_payload), ack_payload, sizeof(ack_payload),
                       &ack_payload_len, LD2410D_CMD_TIMEOUT_MS);
    if (ret != LD2410D_OK) {
        return ret;
    }
    if (ack_payload_len != sizeof(ack_payload)) {
        return LD2410D_ERR_FRAME;
    }

    *value = ld2410d_get_u32_le(ack_payload);
    return LD2410D_OK;
}

ld2410d_ret_t ld2410d_write_param(ld2410d_t *dev, uint16_t param_id,
                                   uint32_t value)
{
    uint8_t payload[6];

    ld2410d_put_u16_le(payload, param_id);
    ld2410d_put_u32_le(payload + 2U, value);
    return command_xfer(dev, LD2410D_CMD_WRITE_PARAM, payload, sizeof(payload),
                        NULL, 0U, NULL, LD2410D_CMD_TIMEOUT_MS);
}

ld2410d_ret_t ld2410d_set_output_mode(ld2410d_t *dev, uint32_t mode)
{
    uint8_t payload[6];
    ld2410d_ret_t ret;

    if (mode != LD2410D_OUTPUT_MODE_ENGINEERING &&
        mode != LD2410D_OUTPUT_MODE_NORMAL) {
        return LD2410D_ERR_PARAM;
    }

    ld2410d_put_u16_le(payload, 0x0000U);
    ld2410d_put_u32_le(payload + 2U, mode);

    ret = command_xfer(dev, LD2410D_CMD_SET_OUTPUT_MODE, payload,
                       sizeof(payload), NULL, 0U, NULL,
                       LD2410D_CMD_TIMEOUT_MS);
    if (ret == LD2410D_OK) {
        dev->output_mode = mode;
    }
    return ret;
}

ld2410d_ret_t ld2410d_save_params(ld2410d_t *dev)
{
    return command_xfer(dev, LD2410D_CMD_SAVE_PARAM, NULL, 0U, NULL, 0U, NULL,
                        LD2410D_CMD_TIMEOUT_MS);
}

ld2410d_ret_t ld2410d_auto_gain(ld2410d_t *dev)
{
    ld2410d_ret_t ret = command_xfer(dev, LD2410D_CMD_AUTO_GAIN, NULL, 0U,
                                     NULL, 0U, NULL,
                                     LD2410D_LONG_CMD_TIMEOUT_MS);
    if (ret != LD2410D_OK) {
        return ret;
    }

    (void)dev->uart.recv(dev->uart.user, dev->rx_buf, LD2410D_RX_BUF_MAX,
                         LD2410D_LONG_CMD_TIMEOUT_MS);
    return LD2410D_OK;
}

ld2410d_ret_t ld2410d_start_auto_threshold(ld2410d_t *dev,
                                            uint16_t trigger_coeff,
                                            uint16_t hold_coeff,
                                            uint16_t static_coeff)
{
    uint8_t payload[6];

    ld2410d_put_u16_le(payload, trigger_coeff);
    ld2410d_put_u16_le(payload + 2U, hold_coeff);
    ld2410d_put_u16_le(payload + 4U, static_coeff);

    return command_xfer(dev, LD2410D_CMD_AUTO_THRESHOLD, payload,
                        sizeof(payload), NULL, 0U, NULL,
                        LD2410D_CMD_TIMEOUT_MS);
}

ld2410d_ret_t ld2410d_query_threshold_progress(ld2410d_t *dev,
                                                uint8_t *progress)
{
    uint8_t payload[2];
    uint16_t payload_len = 0U;
    ld2410d_ret_t ret;

    if (progress == NULL) {
        return LD2410D_ERR_NULL;
    }

    ret = command_xfer(dev, LD2410D_CMD_QUERY_THRESHOLD, NULL, 0U, payload,
                       sizeof(payload), &payload_len, LD2410D_CMD_TIMEOUT_MS);
    if (ret != LD2410D_OK) {
        return ret;
    }
    if (payload_len != sizeof(payload)) {
        return LD2410D_ERR_FRAME;
    }

    *progress = (uint8_t)ld2410d_get_u16_le(payload);
    return LD2410D_OK;
}

ld2410d_ret_t ld2410d_read_basic_config(ld2410d_t *dev)
{
    uint32_t value;
    ld2410d_ret_t ret;

    ret = validate_dev(dev);
    if (ret != LD2410D_OK) {
        return ret;
    }

    ret = ld2410d_read_param(dev, LD2410D_PARAM_MAX_DISTANCE, &value);
    if (ret != LD2410D_OK) {
        return ret;
    }
    dev->basic_cfg.max_distance = (uint8_t)value;

    ret = ld2410d_read_param(dev, LD2410D_PARAM_DISAPPEAR_DELAY, &value);
    if (ret != LD2410D_OK) {
        return ret;
    }
    dev->basic_cfg.disappear_delay = (uint16_t)value;

    return LD2410D_OK;
}

ld2410d_ret_t ld2410d_write_basic_config(ld2410d_t *dev)
{
    ld2410d_ret_t ret;

    ret = validate_dev(dev);
    if (ret != LD2410D_OK) {
        return ret;
    }
    if (dev->basic_cfg.max_distance < 7U || dev->basic_cfg.max_distance > 100U) {
        return LD2410D_ERR_PARAM;
    }

    ret = ld2410d_write_param(dev, LD2410D_PARAM_MAX_DISTANCE,
                              dev->basic_cfg.max_distance);
    if (ret != LD2410D_OK) {
        return ret;
    }

    return ld2410d_write_param(dev, LD2410D_PARAM_DISAPPEAR_DELAY,
                               dev->basic_cfg.disappear_delay);
}

ld2410d_ret_t ld2410d_recv_engineering_frame(ld2410d_t *dev,
                                              ld2410d_engineering_data_t *data,
                                              uint32_t timeout_ms)
{
    ld2410d_ret_t ret;
    uint8_t sync_byte = 0U;
    uint32_t elapsed = 0U;
    uint16_t frame_data_len;
    uint32_t total_len;

    if (data == NULL) {
        return LD2410D_ERR_NULL;
    }

    ret = validate_dev(dev);
    if (ret != LD2410D_OK) {
        return ret;
    }

    dev->uart.flush(dev->uart.user);
    while (elapsed < timeout_ms) {
        uint16_t got = dev->uart.recv(dev->uart.user, &sync_byte, 1U,
                                      LD2410D_SYNC_POLL_MS);
        if (got > 1U) {
            return LD2410D_ERR_IO;
        }
        if (got == 1U && sync_byte == LD2410D_DATA_HEADER_0) {
            dev->rx_buf[0] = sync_byte;
            break;
        }
        elapsed += LD2410D_SYNC_POLL_MS;
    }
    if (elapsed >= timeout_ms) {
        return LD2410D_ERR_TIMEOUT;
    }

    ret = uart_recv_exact(dev, dev->rx_buf + 1U, 5U,
                          LD2410D_INTER_BYTE_TIMEOUT_MS);
    if (ret != LD2410D_OK) {
        return ret;
    }

    if (dev->rx_buf[1] != LD2410D_DATA_HEADER_1 ||
        dev->rx_buf[2] != LD2410D_DATA_HEADER_2 ||
        dev->rx_buf[3] != LD2410D_DATA_HEADER_3) {
        return LD2410D_ERR_FRAME;
    }

    frame_data_len = ld2410d_get_u16_le(&dev->rx_buf[4]);
    total_len = 6U + (uint32_t)frame_data_len + 4U;
    if (total_len > LD2410D_RX_BUF_MAX) {
        return LD2410D_ERR_OVERFLOW;
    }
    if (total_len < 10U) {
        return LD2410D_ERR_FRAME;
    }

    ret = uart_recv_exact(dev, dev->rx_buf + 6U, (uint16_t)(total_len - 6U),
                          LD2410D_INTER_BYTE_TIMEOUT_MS);
    if (ret != LD2410D_OK) {
        return ret;
    }

    dev->rx_len = (uint16_t)total_len;
    ret = ld2410d_parse_engineering_frame(dev->rx_buf, dev->rx_len, data);
    if (ret == LD2410D_OK) {
        dev->eng_data = *data;
    }

    return ret;
}

uint16_t ld2410d_energy_to_db(uint32_t raw_value)
{
    double db_val;

    if (raw_value == 0U) {
        return 0U;
    }

    db_val = 10.0 * log10((double)raw_value);
    return (uint16_t)(db_val * 100.0);
}
