/**
 * @file bl0942.h
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief BL0942 电能计量芯片驱动
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 *
 * @note BL0942 是上海贝岭推出的免校准电能计量芯片，内置 2 路 Σ-Δ ADC，
 *       支持 UART/SPI 两种通信接口，可测量电压/电流有效值、有功功率、
 *       电能脉冲计数、频率、波形采样、过零检测等电参数。
 *
 * @note 寄存器统一 24bit 宽度，通信帧结构为 CMD + ADDR + 3B DATA + CHECKSUM。
 *       UART: 8-N-1, 波特率 4800/9600/19200/38400, 低字节在前
 *       SPI:  Mode 1 (CPOL=0, CPHA=1), MSB 在前, 最大 900kHz
 *
 * @note 用户区寄存器受写保护 (USR_WRPROT @ 0x1D 需先写 0x55 解锁)。
 *       软复位: 写 0x5A5A5A 到 0x1C，或 SPI 连续发 6 个 0xFF。
 */

#ifndef BL0942_H
#define BL0942_H

#include <stdint.h>
#include <stdbool.h>

/* ==================== 错误码 ==================== */

#define BL0942_OK               0
#define BL0942_ERR_CHECKSUM     (-1)
#define BL0942_ERR_TIMEOUT      (-2)
#define BL0942_ERR_WRPROT       (-3)
#define BL0942_ERR_PARAM        (-4)
#define BL0942_ERR_BUSY         (-5)

/* ==================== 通信接口类型 ==================== */

/** @brief BL0942 通信接口类型 */
typedef enum {
    BL0942_IF_UART,             /**< UART 半双工从模式 */
    BL0942_IF_SPI               /**< SPI 从模式 (Mode 1) */
} bl0942_interface_t;

/* ==================== 通信协议命令 ==================== */

/** UART 读命令基址: {0,1,0,1,1,0,A2,A1} → 0x50 + (addr << 4) */
#define BL0942_UART_READ_CMD(addr)     (uint8_t)(0x50 | ((addr) << 4))
/** UART 写命令基址: {1,0,1,0,1,0,A2,A1} → 0xA0 + (addr << 4) */
#define BL0942_UART_WRITE_CMD(addr)    (uint8_t)(0xA0 | ((addr) << 4))
/** UART 全参数包命令: 读CMD + 0xAA */
#define BL0942_UART_PACKET_CMD         0xAA

/** SPI 读命令 */
#define BL0942_SPI_READ_CMD            0x58
/** SPI 写命令 */
#define BL0942_SPI_WRITE_CMD           0xA8

/* ==================== 寄存器地址 ==================== */

/** @brief 电参量寄存器（只读） */
#define BL0942_REG_I_WAVE              0x01    /**< 电流波形 (20bit, 有符号) */
#define BL0942_REG_V_WAVE              0x02    /**< 电压波形 (20bit, 有符号) */
#define BL0942_REG_I_RMS               0x03    /**< 电流有效值 (24bit, 无符号) */
#define BL0942_REG_V_RMS               0x04    /**< 电压有效值 (24bit, 无符号) */
#define BL0942_REG_I_FAST_RMS          0x05    /**< 快速电流有效值 (24bit, 无符号) */
#define BL0942_REG_WATT                0x06    /**< 有功功率 (24bit, 有符号, Bit23=符号位) */
#define BL0942_REG_CF_CNT              0x07    /**< 电能脉冲计数 (24bit, 无符号) */
#define BL0942_REG_FREQ                0x08    /**< 线电压频率 (16bit, 无符号) */
#define BL0942_REG_STATUS              0x09    /**< 工作状态 (10bit) */

/** @brief 用户操作寄存器（读写，受写保护） */
#define BL0942_REG_I_RMSOS             0x12    /**< 电流有效值小信号校正 (8bit) */
#define BL0942_REG_WA_CREEP            0x14    /**< 有功功率防潜动阈值 (8bit) */
#define BL0942_REG_I_FAST_RMS_TH       0x15    /**< 过流阈值 (16bit, 与 I_FAST_RMS[23:8] 比较) */
#define BL0942_REG_I_FAST_RMS_CYC      0x16    /**< 快速有效值刷新周期 (3bit) */
#define BL0942_REG_FREQ_CYC            0x17    /**< 频率刷新周期 (2bit) */
#define BL0942_REG_OT_FUNX             0x18    /**< 输出配置 (6bit) */
#define BL0942_REG_MODE                0x19    /**< 用户模式选择 (10bit) */
#define BL0942_REG_GAIN_CR             0x1A    /**< 电流通道增益 (2bit) */
#define BL0942_REG_SOFT_RESET          0x1C    /**< 软复位 (写 0x5A5A5A 触发) */
#define BL0942_REG_USR_WRPROT          0x1D    /**< 写保护 (写 0x55 解锁) */

/* ==================== 寄存器位域掩码 & 常量 ==================== */

/** MODE (0x19) 位域 */
#define BL0942_MODE_CF_EN_Pos          2
#define BL0942_MODE_CF_EN_Msk          (1 << 2)
#define BL0942_MODE_RMS_UPDATE_SEL_Pos 3
#define BL0942_MODE_RMS_UPDATE_SEL_Msk (1 << 3)
#define BL0942_MODE_FAST_RMS_SEL_Pos   4
#define BL0942_MODE_FAST_RMS_SEL_Msk   (1 << 4)
#define BL0942_MODE_AC_FREQ_SEL_Pos    5
#define BL0942_MODE_AC_FREQ_SEL_Msk    (1 << 5)
#define BL0942_MODE_CF_CNT_CLR_SEL_Pos 6
#define BL0942_MODE_CF_CNT_CLR_SEL_Msk (1 << 6)
#define BL0942_MODE_CF_CNT_ADD_SEL_Pos 7
#define BL0942_MODE_CF_CNT_ADD_SEL_Msk (1 << 7)

/** 波特率选择 */
#define BL0942_UART_4800BPS            0
#define BL0942_UART_9600BPS            1
#define BL0942_UART_19200BPS           2
#define BL0942_UART_38400BPS           3

/** 电流增益 (GAIN_CR @ 0x1A) */
#define BL0942_GAIN_1X                 0       /**< 1 倍增益, 差分 ±700mV */
#define BL0942_GAIN_4X                 1       /**< 4 倍增益, 差分 ±175mV */
#define BL0942_GAIN_16X                2       /**< 16 倍增益 (默认), 差分 ±42mV */
#define BL0942_GAIN_24X                3       /**< 24 倍增益, 差分 ±28mV */

/** 快速有效值刷新周期 (I_FAST_RMS_CYC @ 0x16) */
#define BL0942_FAST_RMS_CYC_0P5        0       /**< 0.5 周波 */
#define BL0942_FAST_RMS_CYC_1          1       /**< 1 周波 (默认) */
#define BL0942_FAST_RMS_CYC_2          2       /**< 2 周波 */
#define BL0942_FAST_RMS_CYC_4          3       /**< 4 周波 */
#define BL0942_FAST_RMS_CYC_8          4       /**< 8 周波 */

/** 频率刷新周期 (FREQ_CYC @ 0x17) */
#define BL0942_FREQ_CYC_2              0       /**< 2 周波 */
#define BL0942_FREQ_CYC_4              1       /**< 4 周波 */
#define BL0942_FREQ_CYC_8              2       /**< 8 周波 */
#define BL0942_FREQ_CYC_16             3       /**< 16 周波 (默认) */

/** 输出功能选择 (OT_FUNX @ 0x18) */
#define BL0942_OT_ENERGY_PULSE         0       /**< 有功能量脉冲 */
#define BL0942_OT_OVERCURRENT_ALARM    1       /**< 过流报警 */
#define BL0942_OT_VOLTAGE_ZERO         2       /**< 电压过零指示 */
#define BL0942_OT_CURRENT_ZERO         3       /**< 电流过零指示 */

/** STATUS (0x09) 位域 */
#define BL0942_STATUS_CF_REVP_F_Pos    0
#define BL0942_STATUS_CF_REVP_F_Msk    (1 << 0)
#define BL0942_STATUS_CREEP_F_Pos      1
#define BL0942_STATUS_CREEP_F_Msk      (1 << 1)
#define BL0942_STATUS_I_ZX_LTH_F_Pos   8
#define BL0942_STATUS_I_ZX_LTH_F_Msk   (1 << 8)
#define BL0942_STATUS_V_ZX_LTH_F_Pos   9
#define BL0942_STATUS_V_ZX_LTH_F_Msk   (1 << 9)

/** 软复位 magic */
#define BL0942_SOFT_RESET_MAGIC        0x5A5A5AU
/** 写保护解锁 magic */
#define BL0942_WRPROT_UNLOCK           0x55U

/** 内部参考电压 (V) */
#define BL0942_VREF                    (1.218f)

/** 电参数转换系数 */
/** I_RMS 转换: I(A) = I_RMS * Vref² / 305978 (默认 16x 增益, 1mΩ 采样) */
#define BL0942_I_RMS_COEFF             (305978.0f)
/** V_RMS 转换: V(V) = V_RMS * Vref² / 73989 */
#define BL0942_V_RMS_COEFF             (73989.0f)
/** WATT 转换: P(W) = WATT * Vref² / (305978 * 73989) * 4096 */
#define BL0942_WATT_COEFF              (4046000.0f)

/* ==================== 电参数数据结构 ==================== */

/**
 * @brief BL0942 全电参数 (UART 数据包 / 逐一读取)
 */
typedef struct {
    int32_t  i_wave;            /**< 电流波形原始值 */
    int32_t  v_wave;            /**< 电压波形原始值 */
    uint32_t i_rms;             /**< 电流有效值原始 */
    uint32_t v_rms;             /**< 电压有效值原始 */
    uint32_t i_fast_rms;        /**< 快速电流有效值原始 */
    int32_t  watt;              /**< 有功功率原始 (有符号) */
    uint32_t cf_cnt;            /**< 电能脉冲计数 */
    uint16_t freq;              /**< 频率原始值 */
    uint16_t status;            /**< 状态原始值 */

    /* 换算值 */
    float    i_rms_a;           /**< 电流有效值 (A) */
    float    v_rms_v;           /**< 电压有效值 (V) */
    float    i_fast_rms_a;      /**< 快速电流有效值 (A) */
    float    watt_w;            /**< 有功功率 (W) */
    float    freq_hz;           /**< 频率 (Hz) */
} bl0942_measurement_t;

/**
 * @brief SPI 帧缓冲
 */
typedef struct {
    uint8_t cmd;
    uint8_t addr;
    uint8_t data_h;
    uint8_t data_m;
    uint8_t data_l;
    uint8_t checksum;
} bl0942_spi_frame_t;

/* ==================== 通信抽象层回调 ==================== */

/**
 * @brief BL0942 通信总线抽象
 *
 * 类似 soft_i2c_bus_t，封装底层 GPIO/UART 操作。
 * 根据接口类型 (uart/spi) 只实现对应的一组回调。
 */
struct bl0942_bus_t {
    bl0942_interface_t interface;   /**< 接口类型 */

    /* --- UART 回调 (interface == BL0942_IF_UART) --- */

    /**
     * @brief UART 发送一个字节
     * @param data 待发送字节
     */
    void (*uart_tx)(uint8_t data);

    /**
     * @brief UART 接收一个字节（带超时）
     * @param timeout_us 超时时间 (μs)
     * @return 接收到的字节，超时返回 -1
     */
    int16_t (*uart_rx)(uint32_t timeout_us);

    /**
     * @brief 延时 (μs)
     */
    void (*delay_us)(uint32_t us);

    /* --- SPI 回调 (interface == BL0942_IF_SPI) --- */

    /**
     * @brief SPI 片选控制
     * @param cs 0=选中, 1=释放
     */
    void (*spi_cs_set)(uint8_t cs);

    /**
     * @brief SPI 时钟控制
     * @param level 时钟电平
     */
    void (*spi_sclk_set)(uint8_t level);

    /**
     * @brief SPI MOSI (主机输出) 控制
     * @param level 数据电平
     */
    void (*spi_mosi_set)(uint8_t level);

    /**
     * @brief SPI MISO (主机输入) 读取
     * @return 数据电平 (0/1)
     */
    uint8_t (*spi_miso_get)(void);

    /** @brief SPI 半周期延时 (μs)，决定 SCLK 频率。900kHz 最大 → half_period_us >= 0.56 */
    uint16_t spi_half_period_us;

    /* --- UART 配置 --- */
    uint8_t  uart_addr;             /**< UART 片选地址 0~3 */
    uint32_t uart_byte_timeout_us;  /**< 字节间超时 (典型 20ms) */
    uint32_t uart_read_delay_us;    /**< 读操作 ADDR 后等待芯片返回的延时 (典型 150us) */

    /* --- 内部状态 (驱动维护) --- */
    uint8_t _wrprot_locked;         /**< 写保护状态: 1=已锁, 0=已解锁 */
};

/* ==================== 驱动 API ==================== */

/**
 * @brief 初始化 BL0942 驱动（设置写保护标志，不操作硬件）
 * @param bus 通信总线配置（需预先填充回调函数和配置）
 * @return BL0942_OK 成功
 */
int8_t bl0942_init(struct bl0942_bus_t *bus);

/**
 * @brief 读取单个寄存器 (24bit)
 * @param bus   总线
 * @param reg   寄存器地址
 * @param value 输出 24bit 值
 * @return BL0942_OK 成功
 */
int8_t bl0942_read_reg(struct bl0942_bus_t *bus, uint8_t reg, uint32_t *value);

/**
 * @brief 写入单个寄存器 (24bit)
 * @param bus   总线
 * @param reg   寄存器地址
 * @param value 24bit 值
 * @return BL0942_OK 成功
 * @note 用户区寄存器 (0x10~0x1F) 需先调用 bl0942_unlock_wrprot()
 */
int8_t bl0942_write_reg(struct bl0942_bus_t *bus, uint8_t reg, uint32_t value);

/**
 * @brief 解锁写保护（写 0x55 到 USR_WRPROT）
 * @param bus 总线
 * @return BL0942_OK 成功
 */
int8_t bl0942_unlock_wrprot(struct bl0942_bus_t *bus);

/**
 * @brief 执行软复位（写 0x5A5A5A 到 SOFT_RESET）
 * @param bus 总线
 * @return BL0942_OK 成功
 */
int8_t bl0942_soft_reset(struct bl0942_bus_t *bus);

/**
 * @brief SPI 软复位（连续发 6 个 0xFF）
 * @param bus 总线 (需为 SPI 模式)
 * @return BL0942_OK 成功
 */
int8_t bl0942_spi_soft_reset(struct bl0942_bus_t *bus);

/**
 * @brief 通过 UART 数据包模式一次性读取全部电参数
 * @param bus  总线 (需为 UART 模式)
 * @param meas 输出 电参数结构
 * @return BL0942_OK 成功
 */
int8_t bl0942_read_packet(struct bl0942_bus_t *bus, bl0942_measurement_t *meas);

/**
 * @brief 逐个寄存器读取全部电参数并换算
 * @param bus  总线
 * @param meas 输出 电参数结构
 * @return BL0942_OK 成功
 */
int8_t bl0942_read_all(struct bl0942_bus_t *bus, bl0942_measurement_t *meas);

/**
 * @brief 将原始值换算为物理量
 * @param meas 含原始值的电参数结构，换算结果直接回填
 */
void bl0942_convert_raw(bl0942_measurement_t *meas);

/**
 * @brief 设置电流通道增益
 * @param bus   总线
 * @param gain  增益值: BL0942_GAIN_1X / 4X / 16X / 24X
 * @return BL0942_OK 成功
 */
int8_t bl0942_set_gain(struct bl0942_bus_t *bus, uint8_t gain);

/**
 * @brief 设置过流保护阈值
 * @param bus      总线
 * @param threshold 阈值 (与 I_FAST_RMS[23:8] 比较)
 * @return BL0942_OK 成功
 */
int8_t bl0942_set_overcurrent_th(struct bl0942_bus_t *bus, uint16_t threshold);

/**
 * @brief 配置 MODE 寄存器
 * @param bus  总线
 * @param mode 10bit mode 值 (仅低 10bit 有效)
 * @return BL0942_OK 成功
 */
int8_t bl0942_set_mode(struct bl0942_bus_t *bus, uint16_t mode);

/**
 * @brief 配置输出功能引脚
 * @param bus      总线
 * @param cf1_sel  CF1 功能: BL0942_OT_ENERGY_PULSE / OVERCURRENT_ALARM / VOLTAGE_ZERO / CURRENT_ZERO
 * @param cf2_sel  CF2 功能
 * @param zx_sel   ZX  功能
 * @return BL0942_OK 成功
 */
int8_t bl0942_set_output(struct bl0942_bus_t *bus,
                         uint8_t cf1_sel, uint8_t cf2_sel, uint8_t zx_sel);

/**
 * @brief 读取 STATUS 寄存器各标志位
 * @param bus    总线
 * @param status 输出 10bit 状态值
 * @return BL0942_OK 成功
 */
int8_t bl0942_read_status(struct bl0942_bus_t *bus, uint16_t *status);

/**
 * @brief 测试通信是否正常（读 STATUS 寄存器并校验返回值合理性）
 * @param bus 总线
 * @return BL0942_OK 通信正常
 */
int8_t bl0942_ping(struct bl0942_bus_t *bus);

/**
 * @brief 计算 checksum
 * @param cmd    命令字节
 * @param addr   地址字节
 * @param data_h 数据高字节
 * @param data_m 数据中字节
 * @param data_l 数据低字节
 * @return checksum (取反和的低 8 位)
 */
static inline uint8_t bl0942_checksum(uint8_t cmd, uint8_t addr,
                                       uint8_t data_h, uint8_t data_m, uint8_t data_l)
{
    return (uint8_t)(~((uint32_t)cmd + addr + data_h + data_m + data_l));
}

#endif /* BL0942_H */
