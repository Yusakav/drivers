/**
 * @file ld2410d.h
 * @brief Platform-independent HLK-LD2410D radar driver.
 */

#ifndef LD2410D_H
#define LD2410D_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LD2410D_FRAME_HEADER_0      0xFDU
#define LD2410D_FRAME_HEADER_1      0xFCU
#define LD2410D_FRAME_HEADER_2      0xFBU
#define LD2410D_FRAME_HEADER_3      0xFAU

#define LD2410D_FRAME_FOOTER_0      0x04U
#define LD2410D_FRAME_FOOTER_1      0x03U
#define LD2410D_FRAME_FOOTER_2      0x02U
#define LD2410D_FRAME_FOOTER_3      0x01U

#define LD2410D_DATA_HEADER_0       0xF4U
#define LD2410D_DATA_HEADER_1       0xF3U
#define LD2410D_DATA_HEADER_2       0xF2U
#define LD2410D_DATA_HEADER_3       0xF1U

#define LD2410D_DATA_FOOTER_0       0xF8U
#define LD2410D_DATA_FOOTER_1       0xF7U
#define LD2410D_DATA_FOOTER_2       0xF6U
#define LD2410D_DATA_FOOTER_3       0xF5U

#define LD2410D_FRAME_OVERHEAD      10U
#define LD2410D_TX_BUF_MAX          64U
#define LD2410D_RX_BUF_MAX          256U
#define LD2410D_GATE_COUNT          16U

#define LD2410D_CMD_READ_VERSION        0x0000U
#define LD2410D_CMD_ENABLE_CFG          0x00FFU
#define LD2410D_CMD_END_CFG             0x00FEU
#define LD2410D_CMD_READ_SN_HEX         0x0016U
#define LD2410D_CMD_READ_SN_CHAR        0x0011U
#define LD2410D_CMD_READ_PARAM          0x0008U
#define LD2410D_CMD_WRITE_PARAM         0x0007U
#define LD2410D_CMD_SET_OUTPUT_MODE     0x0012U
#define LD2410D_CMD_AUTO_THRESHOLD      0x0009U
#define LD2410D_CMD_QUERY_THRESHOLD     0x000AU
#define LD2410D_CMD_REPORT_INTERFERE    0x0014U
#define LD2410D_CMD_SAVE_PARAM          0x00FDU
#define LD2410D_CMD_AUTO_GAIN           0x00EEU

#define LD2410D_PARAM_MAX_DISTANCE      0x0001U
#define LD2410D_PARAM_DISAPPEAR_DELAY   0x0004U
#define LD2410D_PARAM_POWER_INTERFERE   0x0005U
#define LD2410D_PARAM_MOTION_THRESHOLD  0x0010U
#define LD2410D_PARAM_STATIC_THRESHOLD  0x0030U

#define LD2410D_OUTPUT_MODE_ENGINEERING 0x00000004UL
#define LD2410D_OUTPUT_MODE_NORMAL      0x00000064UL

typedef enum {
    LD2410D_OK           =  0,
    LD2410D_ERR_NULL     = -1,
    LD2410D_ERR_PARAM    = -2,
    LD2410D_ERR_TIMEOUT  = -3,
    LD2410D_ERR_FRAME    = -4,
    LD2410D_ERR_ACK      = -5,
    LD2410D_ERR_IO       = -6,
    LD2410D_ERR_OVERFLOW = -7,
    LD2410D_ERR_BUSY     = -8,
} ld2410d_ret_t;

typedef enum {
    LD2410D_DETECT_NONE   = 0x00,
    LD2410D_DETECT_MOVING = 0x01,
    LD2410D_DETECT_STATIC = 0x02,
} ld2410d_detect_status_t;

typedef struct {
    uint8_t  max_distance;
    uint16_t disappear_delay;
} ld2410d_basic_cfg_t;

typedef struct {
    uint32_t motion_threshold[LD2410D_GATE_COUNT];
    uint32_t static_threshold[LD2410D_GATE_COUNT];
} ld2410d_threshold_cfg_t;

typedef struct {
    uint32_t motion_energy;
    uint32_t static_energy;
} ld2410d_gate_energy_t;

typedef struct {
    ld2410d_detect_status_t status;
    uint16_t                distance;
    ld2410d_gate_energy_t   gates[LD2410D_GATE_COUNT];
} ld2410d_engineering_data_t;

typedef struct {
    void     (*send)(void *user, const uint8_t *data, uint16_t len);
    uint16_t (*recv)(void *user, uint8_t *buf, uint16_t max_len,
                     uint32_t timeout_ms);
    void     (*flush)(void *user);
    void     *user;
} ld2410d_uart_ops_t;

typedef struct {
    ld2410d_uart_ops_t        uart;
    uint8_t                   tx_buf[LD2410D_TX_BUF_MAX];
    uint8_t                   rx_buf[LD2410D_RX_BUF_MAX];
    uint16_t                  rx_len;
    ld2410d_basic_cfg_t       basic_cfg;
    ld2410d_threshold_cfg_t   threshold_cfg;
    uint32_t                  output_mode;
    ld2410d_engineering_data_t eng_data;
} ld2410d_t;

ld2410d_ret_t ld2410d_init(ld2410d_t *dev, const ld2410d_uart_ops_t *ops);

ld2410d_ret_t ld2410d_enable_config(ld2410d_t *dev);
ld2410d_ret_t ld2410d_end_config(ld2410d_t *dev);

ld2410d_ret_t ld2410d_read_firmware_version(ld2410d_t *dev,
                                             char *version,
                                             uint8_t version_size,
                                             uint8_t *version_len);
ld2410d_ret_t ld2410d_read_sn_hex(ld2410d_t *dev, uint8_t *sn,
                                   uint8_t sn_size, uint8_t *sn_len);
ld2410d_ret_t ld2410d_read_sn_char(ld2410d_t *dev, char *sn,
                                    uint8_t sn_size, uint8_t *sn_len);

ld2410d_ret_t ld2410d_read_param(ld2410d_t *dev, uint16_t param_id,
                                  uint32_t *value);
ld2410d_ret_t ld2410d_write_param(ld2410d_t *dev, uint16_t param_id,
                                   uint32_t value);
ld2410d_ret_t ld2410d_set_output_mode(ld2410d_t *dev, uint32_t mode);
ld2410d_ret_t ld2410d_save_params(ld2410d_t *dev);
ld2410d_ret_t ld2410d_auto_gain(ld2410d_t *dev);
ld2410d_ret_t ld2410d_start_auto_threshold(ld2410d_t *dev,
                                            uint16_t trigger_coeff,
                                            uint16_t hold_coeff,
                                            uint16_t static_coeff);
ld2410d_ret_t ld2410d_query_threshold_progress(ld2410d_t *dev,
                                                uint8_t *progress);

ld2410d_ret_t ld2410d_read_basic_config(ld2410d_t *dev);
ld2410d_ret_t ld2410d_write_basic_config(ld2410d_t *dev);

ld2410d_ret_t ld2410d_parse_engineering_frame(const uint8_t *buf,
                                               uint16_t len,
                                               ld2410d_engineering_data_t *data);
ld2410d_ret_t ld2410d_recv_engineering_frame(ld2410d_t *dev,
                                              ld2410d_engineering_data_t *data,
                                              uint32_t timeout_ms);

uint16_t ld2410d_energy_to_db(uint32_t raw_value);

#ifdef __cplusplus
}
#endif

#endif
