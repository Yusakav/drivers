/**
 * @file bl0942.c
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief BL0942 电能计量芯片驱动实现
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 */

#include "bl0942.h"
#include <stddef.h>

/* ==================== 内部辅助 ==================== */

/** 24bit 拆为 3 字节 (大端: data_h, data_m, data_l) */
static inline void unpack_24be(uint32_t val, uint8_t *h, uint8_t *m, uint8_t *l)
{
    *h = (uint8_t)((val >> 16) & 0xFF);
    *m = (uint8_t)((val >> 8)  & 0xFF);
    *l = (uint8_t)(val         & 0xFF);
}

/** 3 字节拼为 24bit (大端) */
static inline uint32_t pack_24be(uint8_t h, uint8_t m, uint8_t l)
{
    return ((uint32_t)h << 16) | ((uint32_t)m << 8) | l;
}

/** 3 字节拼为 24bit (小端 — UART 用) */
static inline uint32_t pack_24le(uint8_t b0, uint8_t b1, uint8_t b2)
{
    return (uint32_t)b0 | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16);
}

/* ==================== UART 通信实现 ==================== */

/**
 * @brief UART 发送一个字节并等待字节间延时
 */
static int8_t uart_tx_byte(struct bl0942_bus_t *bus, uint8_t data)
{
    if (!bus->uart_tx) return BL0942_ERR_PARAM;
    bus->uart_tx(data);
    if (bus->delay_us) bus->delay_us(500);
    return BL0942_OK;
}

/**
 * @brief UART 接收一个字节
 * @return >=0 数据, <0 超时
 */
static int16_t uart_rx_byte(struct bl0942_bus_t *bus)
{
    if (!bus->uart_rx) return BL0942_ERR_PARAM;
    return bus->uart_rx(bus->uart_byte_timeout_us);
}

/**
 * @brief UART 写寄存器帧
 *
 * 帧格式: CMD + ADDR + DATA_L + DATA_M + DATA_H + CHECKSUM
 */
static int8_t bl0942_uart_write(struct bl0942_bus_t *bus, uint8_t reg, uint32_t value)
{
    uint8_t cmd      = BL0942_UART_WRITE_CMD(bus->uart_addr);
    uint8_t data_l   = (uint8_t)(value & 0xFF);
    uint8_t data_m   = (uint8_t)((value >> 8) & 0xFF);
    uint8_t data_h   = (uint8_t)((value >> 16) & 0xFF);
    uint8_t checksum = bl0942_checksum(cmd, reg, data_h, data_m, data_l);

    uart_tx_byte(bus, cmd);
    uart_tx_byte(bus, reg);
    uart_tx_byte(bus, data_l);
    uart_tx_byte(bus, data_m);
    uart_tx_byte(bus, data_h);
    uart_tx_byte(bus, checksum);

    return BL0942_OK;
}

/**
 * @brief UART 读寄存器帧
 *
 * 帧格式:
 *   TX: CMD + ADDR
 *   RX: DATA_L + DATA_M + DATA_H + CHECKSUM
 */
static int8_t bl0942_uart_read(struct bl0942_bus_t *bus, uint8_t reg, uint32_t *value)
{
    uint8_t cmd      = BL0942_UART_READ_CMD(bus->uart_addr);
    uint8_t data_l, data_m, data_h, rx_cs;
    int16_t rx;
    uint32_t tmp;

    uart_tx_byte(bus, cmd);
    uart_tx_byte(bus, reg);

    /* 等待芯片准备数据 */
    if (bus->delay_us) bus->delay_us(bus->uart_read_delay_us);

    /* 接收 4 字节: DATA_L, DATA_M, DATA_H, CHECKSUM */
    rx = uart_rx_byte(bus);
    if (rx < 0) return BL0942_ERR_TIMEOUT;
    data_l = (uint8_t)rx;

    rx = uart_rx_byte(bus);
    if (rx < 0) return BL0942_ERR_TIMEOUT;
    data_m = (uint8_t)rx;

    rx = uart_rx_byte(bus);
    if (rx < 0) return BL0942_ERR_TIMEOUT;
    data_h = (uint8_t)rx;

    rx = uart_rx_byte(bus);
    if (rx < 0) return BL0942_ERR_TIMEOUT;
    rx_cs = (uint8_t)rx;

    /* 校验 checksum */
    tmp = bl0942_checksum(cmd, reg, data_h, data_m, data_l);
    if (tmp != rx_cs) return BL0942_ERR_CHECKSUM;

    *value = pack_24le(data_l, data_m, data_h);
    return BL0942_OK;
}

/* ==================== SPI 通信实现 ==================== */

/**
 * @brief SPI 发送/接收一个字节（全双工，Mode 1: CPOL=0 CPHA=1）
 *
 * SCLK 空闲 = 低。数据在上升沿输出/采样，下降沿改变。
 */
static uint8_t spi_transfer_byte(struct bl0942_bus_t *bus, uint8_t tx_data)
{
    uint8_t rx_data = 0;
    uint16_t half   = bus->spi_half_period_us;

    for (int i = 7; i >= 0; i--) {
        /* 设置 MOSI (SCLK 低期间) */
        bus->spi_mosi_set((tx_data >> i) & 1);

        /* SCLK 上升沿 */
        bus->spi_sclk_set(1);
        if (bus->delay_us) bus->delay_us(half);

        /* 采样 MISO */
        if (bus->spi_miso_get()) {
            rx_data |= (1 << i);
        }

        /* SCLK 下降沿 */
        bus->spi_sclk_set(0);
        if (bus->delay_us) bus->delay_us(half);
    }

    return rx_data;
}

/**
 * @brief SPI 发送 N 个字节（不使用 MISO）
 */
static void spi_write_bytes(struct bl0942_bus_t *bus, const uint8_t *data, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        spi_transfer_byte(bus, data[i]);
    }
}

/**
 * @brief SPI 写寄存器帧
 *
 * SDI: CMD(0xA8) + ADDR + DATA_H + DATA_M + DATA_L + CHECKSUM
 */
static int8_t bl0942_spi_write(struct bl0942_bus_t *bus, uint8_t reg, uint32_t value)
{
    uint8_t data_h, data_m, data_l;
    uint8_t checksum;
    uint8_t buf[6];

    unpack_24be(value, &data_h, &data_m, &data_l);
    checksum = bl0942_checksum(BL0942_SPI_WRITE_CMD, reg, data_h, data_m, data_l);

    buf[0] = BL0942_SPI_WRITE_CMD;
    buf[1] = reg;
    buf[2] = data_h;
    buf[3] = data_m;
    buf[4] = data_l;
    buf[5] = checksum;

    if (bus->spi_cs_set) bus->spi_cs_set(0);
    spi_write_bytes(bus, buf, 6);
    if (bus->spi_cs_set) bus->spi_cs_set(1);

    return BL0942_OK;
}

/**
 * @brief SPI 读寄存器帧
 *
 * SDI: CMD(0x58) + ADDR
 * SDO: DATA_H + DATA_M + DATA_L + CHECKSUM
 */
static int8_t bl0942_spi_read(struct bl0942_bus_t *bus, uint8_t reg, uint32_t *value)
{
    uint8_t data_h, data_m, data_l, rx_cs;
    uint8_t cs_calc;

    if (bus->spi_cs_set) bus->spi_cs_set(0);

    /* 发送读命令和地址，同时丢弃 SDO 返回的垃圾 */
    spi_transfer_byte(bus, BL0942_SPI_READ_CMD);
    spi_transfer_byte(bus, reg);

    /* 连续读 4 字节（发送 dummy 0x00 驱动时钟） */
    data_h = spi_transfer_byte(bus, 0x00);
    data_m = spi_transfer_byte(bus, 0x00);
    data_l = spi_transfer_byte(bus, 0x00);
    rx_cs  = spi_transfer_byte(bus, 0x00);

    if (bus->spi_cs_set) bus->spi_cs_set(1);

    /* 校验 checksum */
    cs_calc = bl0942_checksum(BL0942_SPI_READ_CMD, reg, data_h, data_m, data_l);
    if (cs_calc != rx_cs) return BL0942_ERR_CHECKSUM;

    *value = pack_24be(data_h, data_m, data_l);
    return BL0942_OK;
}

/* ==================== 公开 API ==================== */

int8_t bl0942_init(struct bl0942_bus_t *bus)
{
    if (!bus) return BL0942_ERR_PARAM;
    bus->_wrprot_locked = 1;  /* 上电默认锁定 */
    return BL0942_OK;
}

int8_t bl0942_read_reg(struct bl0942_bus_t *bus, uint8_t reg, uint32_t *value)
{
    if (!bus || !value) return BL0942_ERR_PARAM;

    if (bus->interface == BL0942_IF_UART) {
        return bl0942_uart_read(bus, reg, value);
    } else {
        return bl0942_spi_read(bus, reg, value);
    }
}

int8_t bl0942_write_reg(struct bl0942_bus_t *bus, uint8_t reg, uint32_t value)
{
    int8_t ret;

    if (!bus) return BL0942_ERR_PARAM;

    /* 用户区寄存器 (0x10~0x1F) 需要写保护解锁 */
    if (reg >= 0x10 && reg <= 0x1F && reg != BL0942_REG_USR_WRPROT) {
        if (bus->_wrprot_locked) {
            ret = bl0942_unlock_wrprot(bus);
            if (ret != BL0942_OK) return ret;
        }
    }

    if (bus->interface == BL0942_IF_UART) {
        ret = bl0942_uart_write(bus, reg, value);
    } else {
        ret = bl0942_spi_write(bus, reg, value);
    }

    /* 写 USR_WRPROT 后更新锁定状态 */
    if (reg == BL0942_REG_USR_WRPROT && ret == BL0942_OK) {
        bus->_wrprot_locked = (value != BL0942_WRPROT_UNLOCK);
    }

    return ret;
}

int8_t bl0942_unlock_wrprot(struct bl0942_bus_t *bus)
{
    int8_t ret;

    if (!bus) return BL0942_ERR_PARAM;

    /*
     * USR_WRPROT 本身不受写保护限制，可直接写。
     * 调用底层 write 而非 bl0942_write_reg 避免递归加锁逻辑。
     */
    if (bus->interface == BL0942_IF_UART) {
        ret = bl0942_uart_write(bus, BL0942_REG_USR_WRPROT, BL0942_WRPROT_UNLOCK);
    } else {
        ret = bl0942_spi_write(bus, BL0942_REG_USR_WRPROT, BL0942_WRPROT_UNLOCK);
    }

    if (ret == BL0942_OK) {
        bus->_wrprot_locked = 0;
    }

    return ret;
}

int8_t bl0942_soft_reset(struct bl0942_bus_t *bus)
{
    if (!bus) return BL0942_ERR_PARAM;

    /* 软复位不受写保护限制 */
    if (bus->interface == BL0942_IF_UART) {
        return bl0942_uart_write(bus, BL0942_REG_SOFT_RESET, BL0942_SOFT_RESET_MAGIC);
    } else {
        return bl0942_spi_write(bus, BL0942_REG_SOFT_RESET, BL0942_SOFT_RESET_MAGIC);
    }
}

int8_t bl0942_spi_soft_reset(struct bl0942_bus_t *bus)
{
    if (!bus || bus->interface != BL0942_IF_SPI) return BL0942_ERR_PARAM;

    if (bus->spi_cs_set) bus->spi_cs_set(0);
    for (int i = 0; i < 6; i++) {
        spi_transfer_byte(bus, 0xFF);
    }
    if (bus->spi_cs_set) bus->spi_cs_set(1);

    bus->_wrprot_locked = 1;
    return BL0942_OK;
}

int8_t bl0942_read_packet(struct bl0942_bus_t *bus, bl0942_measurement_t *meas)
{
    uint8_t cmd;
    int16_t rx;
    uint8_t buf[22];
    uint32_t checksum_calc;
    uint8_t  checksum_rx;

    if (!bus || !meas) return BL0942_ERR_PARAM;
    if (bus->interface != BL0942_IF_UART) return BL0942_ERR_PARAM;

    /* 发送全参数包命令: READ_CMD + 0xAA */
    cmd = BL0942_UART_READ_CMD(bus->uart_addr);
    uart_tx_byte(bus, cmd);
    uart_tx_byte(bus, BL0942_UART_PACKET_CMD);

    if (bus->delay_us) bus->delay_us(bus->uart_read_delay_us);

    /* 接收 22 字节 */
    for (int i = 0; i < 22; i++) {
        rx = uart_rx_byte(bus);
        if (rx < 0) return BL0942_ERR_TIMEOUT;
        buf[i] = (uint8_t)rx;
    }

    /* 校验 HEAD */
    if (buf[0] != 0x55) return BL0942_ERR_CHECKSUM;

    /* 校验 checksum (所有字节累加取反) */
    checksum_calc = 0;
    for (int i = 0; i < 21; i++) {
        checksum_calc += buf[i];
    }
    checksum_rx = buf[21];
    if ((uint8_t)(~checksum_calc) != checksum_rx) return BL0942_ERR_CHECKSUM;

    /* 解析数据包 (小端序) */
    meas->i_rms      = pack_24le(buf[1],  buf[2],  buf[3]);
    meas->v_rms      = pack_24le(buf[4],  buf[5],  buf[6]);
    meas->i_fast_rms = pack_24le(buf[7],  buf[8],  buf[9]);
    meas->watt       = (int32_t)pack_24le(buf[10], buf[11], buf[12]);
    meas->cf_cnt     = pack_24le(buf[13], buf[14], buf[15]);
    meas->freq       = (uint16_t)(buf[16] | ((uint16_t)buf[17] << 8));
    meas->status     = (uint16_t)buf[19];

    /* 波形数据不在包中，清零 */
    meas->i_wave = 0;
    meas->v_wave = 0;

    /* 换算物理量 */
    bl0942_convert_raw(meas);

    return BL0942_OK;
}

int8_t bl0942_read_all(struct bl0942_bus_t *bus, bl0942_measurement_t *meas)
{
    int8_t ret;
    uint32_t val;

    if (!bus || !meas) return BL0942_ERR_PARAM;

    /* 逐个读取电参量寄存器 */
    ret = bl0942_read_reg(bus, BL0942_REG_I_RMS, &val);
    if (ret != BL0942_OK) return ret;
    meas->i_rms = val;

    ret = bl0942_read_reg(bus, BL0942_REG_V_RMS, &val);
    if (ret != BL0942_OK) return ret;
    meas->v_rms = val;

    ret = bl0942_read_reg(bus, BL0942_REG_I_FAST_RMS, &val);
    if (ret != BL0942_OK) return ret;
    meas->i_fast_rms = val;

    ret = bl0942_read_reg(bus, BL0942_REG_WATT, &val);
    if (ret != BL0942_OK) return ret;
    meas->watt = (int32_t)val;

    ret = bl0942_read_reg(bus, BL0942_REG_CF_CNT, &val);
    if (ret != BL0942_OK) return ret;
    meas->cf_cnt = val;

    ret = bl0942_read_reg(bus, BL0942_REG_FREQ, &val);
    if (ret != BL0942_OK) return ret;
    meas->freq = (uint16_t)(val & 0xFFFF);

    ret = bl0942_read_reg(bus, BL0942_REG_STATUS, &val);
    if (ret != BL0942_OK) return ret;
    meas->status = (uint16_t)(val & 0x3FF);

    /* 波形寄存器（可选） */
    ret = bl0942_read_reg(bus, BL0942_REG_I_WAVE, &val);
    if (ret == BL0942_OK) meas->i_wave = (int32_t)(val & 0xFFFFF);
    else meas->i_wave = 0;

    ret = bl0942_read_reg(bus, BL0942_REG_V_WAVE, &val);
    if (ret == BL0942_OK) meas->v_wave = (int32_t)(val & 0xFFFFF);
    else meas->v_wave = 0;

    bl0942_convert_raw(meas);
    return BL0942_OK;
}

void bl0942_convert_raw(bl0942_measurement_t *meas)
{
    if (!meas) return;

    /* I_RMS → A */
    meas->i_rms_a = (float)meas->i_rms * BL0942_VREF * BL0942_VREF / BL0942_I_RMS_COEFF;

    /* V_RMS → V */
    meas->v_rms_v = (float)meas->v_rms * BL0942_VREF * BL0942_VREF / BL0942_V_RMS_COEFF;

    /* I_FAST_RMS → A (公式同 I_RMS) */
    meas->i_fast_rms_a = (float)meas->i_fast_rms * BL0942_VREF * BL0942_VREF / BL0942_I_RMS_COEFF;

    /* WATT → W (有符号) */
    if (meas->watt & 0x800000) {
        /* 负功率: 取补码 */
        int32_t abs_watt = (int32_t)((meas->watt ^ 0x7FFFFF) + 1);
        meas->watt_w = -(float)abs_watt * BL0942_VREF * BL0942_VREF / BL0942_WATT_COEFF;
    } else {
        meas->watt_w = (float)meas->watt * BL0942_VREF * BL0942_VREF / BL0942_WATT_COEFF;
    }

    /* FREQ → Hz: f = fs / (2 * FREQ), fs 取决于 AC_FREQ_SEL
     *  50Hz: fs ≈ 3.2MHz → f = 1.6e6 / FREQ
     *  60Hz: fs ≈ 3.84MHz → f = 1.92e6 / FREQ
     *
     *  简化: 手册公式 f(Hz) = fs / (2 × FREQ)
     *  默认 50Hz 场景, FREQ 典型值 0x4E20→50Hz
     *  实际使用 fs = FREQ × 2 × 50Hz 反推, 此处用近似:
     */
    if (meas->freq > 0) {
        meas->freq_hz = 1600000.0f / (float)meas->freq;  /* 默认 50Hz 时钟 */
    } else {
        meas->freq_hz = 0.0f;
    }
}

/* ==================== 便捷 API ==================== */

int8_t bl0942_set_gain(struct bl0942_bus_t *bus, uint8_t gain)
{
    if (!bus || gain > 3) return BL0942_ERR_PARAM;
    return bl0942_write_reg(bus, BL0942_REG_GAIN_CR, (uint32_t)(gain & 0x03));
}

int8_t bl0942_set_overcurrent_th(struct bl0942_bus_t *bus, uint16_t threshold)
{
    if (!bus) return BL0942_ERR_PARAM;
    return bl0942_write_reg(bus, BL0942_REG_I_FAST_RMS_TH, (uint32_t)threshold);
}

int8_t bl0942_set_mode(struct bl0942_bus_t *bus, uint16_t mode)
{
    if (!bus) return BL0942_ERR_PARAM;
    return bl0942_write_reg(bus, BL0942_REG_MODE, (uint32_t)(mode & 0x03FF));
}

int8_t bl0942_set_output(struct bl0942_bus_t *bus,
                         uint8_t cf1_sel, uint8_t cf2_sel, uint8_t zx_sel)
{
    uint32_t val;

    if (!bus) return BL0942_ERR_PARAM;

    val = ((uint32_t)(cf1_sel & 0x03) << 0)
        | ((uint32_t)(cf2_sel & 0x03) << 2)
        | ((uint32_t)(zx_sel  & 0x03) << 4);

    return bl0942_write_reg(bus, BL0942_REG_OT_FUNX, val);
}

int8_t bl0942_read_status(struct bl0942_bus_t *bus, uint16_t *status)
{
    int8_t ret;
    uint32_t val;

    if (!bus || !status) return BL0942_ERR_PARAM;

    ret = bl0942_read_reg(bus, BL0942_REG_STATUS, &val);
    if (ret != BL0942_OK) return ret;

    *status = (uint16_t)(val & 0x03FF);
    return BL0942_OK;
}

int8_t bl0942_ping(struct bl0942_bus_t *bus)
{
    uint16_t status;
    int8_t ret;

    ret = bl0942_read_status(bus, &status);
    if (ret != BL0942_OK) return ret;

    /* STATUS 上电默认值 0x000，读取到有效 10bit 即可 */
    if (status > 0x03FF) return BL0942_ERR_CHECKSUM;

    return BL0942_OK;
}
