/**
 * @file ld2410d_drv.h
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief HLK-LD2410D 人体存在雷达驱动 — 寄存器/命令宏定义、数据结构、API声明
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 *
 * @note 硬件引脚定义
 *    LD2410D_OUT          GPIO (用户自定义)
 *    LD2410D_UART_TX      MCU UART RX
 *    LD2410D_UART_RX      MCU UART TX
 *
 * @note 通信规格
 *    波特率: 115200, 1 停止位, 无校验, 小端格式
 */

#ifndef LD2410D_DRV_H
#define LD2410D_DRV_H

#include <stdint.h>

/* ==================== 帧常量 ==================== */

/** @brief 命令帧头 (小端: FD FC FB FA) */
#define LD2410D_FRAME_HEADER_0      0xFDU
#define LD2410D_FRAME_HEADER_1      0xFCU
#define LD2410D_FRAME_HEADER_2      0xFBU
#define LD2410D_FRAME_HEADER_3      0xFAU

/** @brief 命令帧尾 (小端: 04 03 02 01) */
#define LD2410D_FRAME_FOOTER_0      0x04U
#define LD2410D_FRAME_FOOTER_1      0x03U
#define LD2410D_FRAME_FOOTER_2      0x02U
#define LD2410D_FRAME_FOOTER_3      0x01U

/** @brief 工程模式数据帧头 (小端: F4 F3 F2 F1) */
#define LD2410D_DATA_HEADER_0       0xF4U
#define LD2410D_DATA_HEADER_1       0xF3U
#define LD2410D_DATA_HEADER_2       0xF2U
#define LD2410D_DATA_HEADER_3       0xF1U

/** @brief 工程模式数据帧尾 (小端: F8 F7 F6 F5) */
#define LD2410D_DATA_FOOTER_0       0xF8U
#define LD2410D_DATA_FOOTER_1       0xF7U
#define LD2410D_DATA_FOOTER_2       0xF6U
#define LD2410D_DATA_FOOTER_3       0xF5U

/** @brief 命令帧开销: 4 字节帧头 + 2 字节长度 + 4 字节帧尾 */
#define LD2410D_FRAME_OVERHEAD      10U

/** @brief 发送缓冲区最大长度 */
#define LD2410D_TX_BUF_MAX          64U

/** @brief 接收缓冲区最大长度 */
#define LD2410D_RX_BUF_MAX          256U

/* ==================== 命令字 ==================== */

/** @brief 读取固件版本 */
#define LD2410D_CMD_READ_VERSION        0x0000U
/** @brief 使能配置 (任何其他命令前必须发送) */
#define LD2410D_CMD_ENABLE_CFG          0x00FFU
/** @brief 结束配置 (恢复工作模式) */
#define LD2410D_CMD_END_CFG             0x00FEU
/** @brief 读取序列号 (十六进制形式) */
#define LD2410D_CMD_READ_SN_HEX         0x0016U
/** @brief 读取序列号 (字符形式) */
#define LD2410D_CMD_READ_SN_CHAR        0x0011U
/** @brief 读取传感器参数 */
#define LD2410D_CMD_READ_PARAM          0x0008U
/** @brief 配置传感器参数 */
#define LD2410D_CMD_WRITE_PARAM         0x0007U
/** @brief 配置数据输出模式 */
#define LD2410D_CMD_SET_OUTPUT_MODE     0x0012U
/** @brief 开始自动门限生成 */
#define LD2410D_CMD_AUTO_THRESHOLD      0x0009U
/** @brief 查询自动门限生成进度 */
#define LD2410D_CMD_QUERY_THRESHOLD     0x000AU
/** @brief 上报自动门限干扰 */
#define LD2410D_CMD_REPORT_INTERFERE    0x0014U
/** @brief 参数保存 (掉电保存) */
#define LD2410D_CMD_SAVE_PARAM          0x00FDU
/** @brief 上电自动增益调节 */
#define LD2410D_CMD_AUTO_GAIN           0x00EEU

/* ==================== 参数 ID ==================== */

/** @brief 最大距离 (7~100, 单位 0.1m) */
#define LD2410D_PARAM_MAX_DISTANCE      0x0001U
/** @brief 目标消失延迟 (0~65535, 单位秒) */
#define LD2410D_PARAM_DISAPPEAR_DELAY   0x0004U
/** @brief 电源干扰报警 (只读: 0=未进行, 1=无干扰, 2=有干扰) */
#define LD2410D_PARAM_POWER_INTERFERE   0x0005U
/** @brief 运动触发门限基地址 (距离门 0~15, ID = 0x0010 + gate) */
#define LD2410D_PARAM_MOTION_THRESHOLD  0x0010U
/** @brief 微动门限基地址 (距离门 0~15, ID = 0x0030 + gate) */
#define LD2410D_PARAM_STATIC_THRESHOLD  0x0030U

/* ==================== 输出模式 ==================== */

/** @brief 工程模式 (输出能量值数据帧) */
#define LD2410D_OUTPUT_MODE_ENGINEERING 0x00000004UL
/** @brief 正常工作模式 (输出文本) */
#define LD2410D_OUTPUT_MODE_NORMAL      0x00000064UL

/* ==================== 检测结果枚举 ==================== */

/** @brief 工程模式数据帧中的检测结果 */
enum ld2410d_detect_status_e {
    LD2410D_DETECT_NONE     = 0x00, /**< 无人 */
    LD2410D_DETECT_MOVING   = 0x01, /**< 有人 (运动) */
    LD2410D_DETECT_STATIC   = 0x02, /**< 有人 (静止) */
};

/* ==================== 返回码 ==================== */

enum ld2410d_ret_e {
    LD2410D_OK              =  0, /**< 成功 */
    LD2410D_ERR_TIMEOUT     = -1, /**< 接收超时 */
    LD2410D_ERR_FRAME       = -2, /**< 帧格式错误 */
    LD2410D_ERR_ACK         = -3, /**< ACK 返回失败 */
    LD2410D_ERR_PARAM       = -4, /**< 参数无效 */
    LD2410D_ERR_BUSY        = -5, /**< 设备忙 */
};

/* ==================== 传感器参数配置 ==================== */

/**
 * @brief 传感器参数 — 最大距离 / 消失延迟
 */
typedef struct {
    uint8_t  max_distance;       /**< 最大探测距离 (7~100, 单位 0.1m, 即 0.7m~10m) */
    uint16_t disappear_delay;    /**< 目标消失延迟时间 (秒, 0~65535) */
} ld2410d_basic_cfg_t;

/**
 * @brief 传感器参数 — 门限
 */
typedef struct {
    uint32_t motion_threshold[16]; /**< 运动触发门限 (距离门 0~15, 原始值 M = 10^(N/10)) */
    uint32_t static_threshold[16]; /**< 微动门限 (距离门 0~15, 原始值 M = 10^(N/10)) */
} ld2410d_threshold_cfg_t;

/* ==================== 工程模式数据 ==================== */

/**
 * @brief 单个距离门的能量值
 */
typedef struct {
    uint32_t motion_energy;      /**< 运动能量值 (原始 M 值) */
    uint32_t static_energy;      /**< 微动 & 静止能量值 (原始 M 值) */
} ld2410d_gate_energy_t;

/**
 * @brief 工程模式完整数据帧 (解析后)
 */
typedef struct {
    enum ld2410d_detect_status_e status;       /**< 检测状态 */
    uint16_t                      distance;     /**< 目标距离 (cm) */
    ld2410d_gate_energy_t         gates[16];    /**< 16 个距离门能量值 */
} ld2410d_engineering_data_t;

/* ==================== UART 操作抽象 ==================== */

/**
 * @brief UART 接口操作集 (平台相关, 需用户实现)
 */
struct ld2410d_uart_ops_t {
    /**
     * @brief 发送字节序列 (阻塞)
     * @param data 数据指针
     * @param len  数据长度
     */
    void     (*send)(const uint8_t *data, uint16_t len);

    /**
     * @brief 接收字节序列
     * @param buf         接收缓冲区
     * @param max_len     最大接收长度
     * @param timeout_ms  超时 (ms)
     * @return 实际接收到的字节数
     */
    uint16_t (*recv)(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms);

    /**
     * @brief 清空接收缓冲区
     */
    void     (*flush)(void);
};

/* ==================== 设备句柄 ==================== */

/**
 * @brief LD2410D 设备句柄 (驱动内部状态)
 */
struct ld2410d_dev_t {
    struct ld2410d_uart_ops_t uart;         /**< UART 操作集 */
    uint8_t                   tx_buf[LD2410D_TX_BUF_MAX]; /**< 发送缓冲区 */
    uint8_t                   rx_buf[LD2410D_RX_BUF_MAX]; /**< 接收缓冲区 */
    uint16_t                  rx_len;       /**< 实际接收长度 */

    /* 参数缓存 */
    ld2410d_basic_cfg_t       basic_cfg;    /**< 基本参数缓存 */
    ld2410d_threshold_cfg_t   threshold_cfg;/**< 门限参数缓存 */
    uint8_t                   output_mode;  /**< 当前输出模式 (0=未知) */

    /* 工程模式数据 */
    ld2410d_engineering_data_t eng_data;    /**< 最近一次工程模式数据 */
};

/* ==================== API 声明 ==================== */

/**
 * @brief 初始化 LD2410D 设备句柄
 *
 * @param dev  设备句柄指针
 * @param ops  UART 操作集 (需用户预先填充 send / recv / flush)
 */
void ld2410d_init (struct ld2410d_dev_t *dev,
                   const struct ld2410d_uart_ops_t *ops);

/**
 * @brief 使能配置模式 (所有配置命令前必须调用)
 *
 * @param dev 设备句柄
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_enable_config (struct ld2410d_dev_t *dev);

/**
 * @brief 结束配置模式 (恢复工作模式)
 *
 * @param dev 设备句柄
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_end_config (struct ld2410d_dev_t *dev);

/**
 * @brief 读取固件版本
 *
 * @param dev    设备句柄
 * @param ver    输出版本字符串缓冲区 (需至少 16 字节)
 * @param ver_len 版本字符串长度输出
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_read_firmware_version (struct ld2410d_dev_t *dev,
                                       char *ver, uint8_t *ver_len);

/**
 * @brief 读取序列号 (十六进制)
 *
 * @param dev 设备句柄
 * @param sn  输出 4 字节序列号缓冲区
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_read_sn_hex (struct ld2410d_dev_t *dev, uint8_t *sn);

/**
 * @brief 读取序列号 (字符形式)
 *
 * @param dev     设备句柄
 * @param sn_str  输出 SN 字符串缓冲区 (需至少 16 字节)
 * @param sn_len  实际字符串长度输出
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_read_sn_char (struct ld2410d_dev_t *dev,
                              char *sn_str, uint8_t *sn_len);

/**
 * @brief 读取单个传感器参数 (32 位)
 *
 * @param dev       设备句柄
 * @param param_id  参数 ID
 * @param value     输出参数值
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_read_param (struct ld2410d_dev_t *dev,
                            uint16_t param_id, uint32_t *value);

/**
 * @brief 写入单个传感器参数 (32 位)
 *
 * @param dev       设备句柄
 * @param param_id  参数 ID
 * @param value     参数值
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_write_param (struct ld2410d_dev_t *dev,
                             uint16_t param_id, uint32_t value);

/**
 * @brief 设置数据输出模式
 *
 * @param dev   设备句柄
 * @param mode  输出模式 (LD2410D_OUTPUT_MODE_ENGINEERING / LD2410D_OUTPUT_MODE_NORMAL)
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_set_output_mode (struct ld2410d_dev_t *dev, uint32_t mode);

/**
 * @brief 保存参数到 Flash (掉电保存)
 *
 * @param dev 设备句柄
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_save_params (struct ld2410d_dev_t *dev);

/**
 * @brief 上电自动增益调节
 *
 * @param dev 设备句柄
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_auto_gain (struct ld2410d_dev_t *dev);

/**
 * @brief 开始自动门限生成
 *
 * @param dev               设备句柄
 * @param trigger_coeff     触发门限生成系数 (10 倍放大, 范围 0x000A~0x00C8, 如 3 → 0x001E)
 * @param hold_coeff        保持门限生成系数
 * @param static_coeff      微动门限生成系数
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_start_auto_threshold (struct ld2410d_dev_t *dev,
                                      uint16_t trigger_coeff,
                                      uint16_t hold_coeff,
                                      uint16_t static_coeff);

/**
 * @brief 查询自动门限生成进度
 *
 * @param dev       设备句柄
 * @param progress  输出进度百分比 (0~100)
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_query_threshold_progress (struct ld2410d_dev_t *dev,
                                          uint8_t *progress);

/**
 * @brief 读取基本配置并缓存到 dev->basic_cfg
 *
 * @param dev 设备句柄
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_read_basic_config (struct ld2410d_dev_t *dev);

/**
 * @brief 将缓存的配置写入传感器
 *
 * @param dev 设备句柄
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_write_basic_config (struct ld2410d_dev_t *dev);

/**
 * @brief 解析工程模式数据帧 (从 dev->rx_buf)
 *
 * 调用前需先通过 dev->uart.recv 填充 rx_buf
 *
 * @param dev  设备句柄
 * @param data 输出解析后的数据
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_parse_engineering_frame (struct ld2410d_dev_t *dev,
                                         ld2410d_engineering_data_t *data);

/**
 * @brief 等待并接收一帧工程模式数据
 *
 * 调用 uart.flush() 清空缓冲区后, 等待工程模式数据帧头,
 * 然后接收整帧并解析。
 *
 * @param dev      设备句柄
 * @param data     输出解析后的数据
 * @param timeout  超时 (ms)
 * @return LD2410D_OK 成功, 其他为错误码
 */
int8_t ld2410d_recv_engineering_frame (struct ld2410d_dev_t *dev,
                                        ld2410d_engineering_data_t *data,
                                        uint32_t timeout);

/**
 * @brief 将能量原始值 M 转换为 dB 值
 *
 * 转换公式: N = 10 * log10(M)
 *
 * @param raw_value 原始 M 值
 * @return dB 值 (单位 0.01 dB, 即 3662 表示 36.62 dB)
 */
uint16_t ld2410d_energy_to_db (uint32_t raw_value);

#endif /* LD2410D_DRV_H */
