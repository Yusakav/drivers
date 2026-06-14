/**
 * @file bl0942_app.c
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief BL0942 应用示例 — 状态机驱动的电参数采集与监控
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 *
 * @note 硬件引脚定义
 *       BL0942_SEL         PB8      (0=UART, 1=SPI, 内部下拉)
 *       BL0942_TX_SDO      PB9      (UART TX / SPI DO)
 *       BL0942_RX_SDI      PB10     (UART RX / SPI DI)
 *       BL0942_SCLK_BPS    PB11     (SPI 时钟 / UART 波特率选择)
 *       BL0942_CF1         PA0      (电能脉冲/过流报警/过零 可选)
 *
 * @note 本示例使用 UART 模式，波特率 9600bps (SCLK_BPS=1, MODE[9:8]=00 默认)。
 *       若使用 SPI，需将 bus.interface 设为 BL0942_IF_SPI 并实现 SPI 回调。
 */

#include "bl0942_app.h"
#include "bl0942.h"

/* --- 平台依赖头文件 (按实际 MCU 修改) --- */
#include "ch32x035.h"
#include "gpio.h"
#include "timer.h"

/* --- 日志系统 --- */
#define LOG_LEVEL LOG_LVL_DEBUG
#define LOG_TAG "BL0942"
#include "logger.h"

#include <string.h>

/* ==================== 全局应用实例 ==================== */

struct bl0942_app_t bl0942_app;

/* ==================== 状态机名称表 (调试用) ==================== */

static const char *const bl0942_state_names[] = {
    "BL0942_APP_STATE_INIT",
    "BL0942_APP_STATE_CHECK",
    "BL0942_APP_STATE_CONFIGURE",
    "BL0942_APP_STATE_MEASURE",
    "BL0942_APP_STATE_OVERCURRENT",
    "BL0942_APP_STATE_DUMP",
    "BL0942_APP_STATE_IDLE",
    "BL0942_APP_STATE_ERROR",
};

/* ==================== 硬件抽象层 — UART GPIO 回调 ==================== */

/**
 * @brief UART TX: 软件模拟 UART 发送一个字节
 *
 * 波特率 = 9600 → 位周期 ≈ 104μs。
 * 格式: 起始位(0) + 8bit LSB first + 停止位(1)
 */
static void bl0942_uart_tx_byte(uint8_t data)
{
    uint32_t bit_delay_us = 104;  /* 9600bps */

    /* 起始位: 拉低 */
    GPIO_WriteBit(GPIOB, GPIO_Pin_9, 0);
    Delay_Us(bit_delay_us);

    /* 8 数据位, LSB first */
    for (int i = 0; i < 8; i++) {
        GPIO_WriteBit(GPIOB, GPIO_Pin_9, (data >> i) & 0x01);
        Delay_Us(bit_delay_us);
    }

    /* 停止位: 拉高 */
    GPIO_WriteBit(GPIOB, GPIO_Pin_9, 1);
    Delay_Us(bit_delay_us);
}

/**
 * @brief UART RX: 软件模拟 UART 接收一个字节 (带超时)
 * @param timeout_us 超时 (μs)
 * @return >=0 接收字节, -1 超时
 */
static int16_t bl0942_uart_rx_byte(uint32_t timeout_us)
{
    uint32_t bit_delay_us = 104;  /* 9600bps */
    uint32_t elapsed = 0;
    uint8_t  data = 0;

    /* 等待起始位 (低电平) */
    while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_10)) {
        Delay_Us(1);
        elapsed++;
        if (elapsed > timeout_us) return -1;
    }

    /* 延时到起始位中央 */
    Delay_Us(bit_delay_us / 2);

    /* 采样 8 数据位, LSB first */
    for (int i = 0; i < 8; i++) {
        Delay_Us(bit_delay_us);
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_10)) {
            data |= (1 << i);
        }
    }

    /* 等待停止位结束 */
    Delay_Us(bit_delay_us);

    return (int16_t)data;
}

/* ==================== 默认应用配置 ==================== */

static const bl0942_app_config_t default_config = {
    .current_gain           = BL0942_GAIN_16X,
    .overcurrent_threshold  = 0xFFFF,           /* 默认关闭过流检测 */
    .fast_rms_cycle         = BL0942_FAST_RMS_CYC_1,
    .freq_cycle             = BL0942_FREQ_CYC_16,
    .cf1_func               = BL0942_OT_ENERGY_PULSE,
    .cf2_func               = BL0942_OT_VOLTAGE_ZERO,
    .zx_func                = BL0942_OT_CURRENT_ZERO,
    .ac_freq_hz             = 50,
    .rms_update_400ms       = 0,                /* 800ms 刷新 (更高精度) */
    .mode                   = 0,
};

/* ==================== 内部辅助函数 ==================== */

static void set_timeout(uint64_t timeout_ms, bl0942_app_state_t next_state)
{
    bl0942_app.timeout       = get_time().val + timeout_ms;
    bl0942_app.timeout_state = next_state;
}

static void clear_timeout(void)
{
    bl0942_app.timeout = 0;
}

/**
 * @brief 切换状态机状态，自动记录耗时
 */
static void set_state(bl0942_app_state_t next_state)
{
    bl0942_app_state_t last = bl0942_app.task_state;

    if (last == next_state) return;

    /* 记录上一阶段耗时 */
    if (last < BL0942_APP_STATE_COUNT) {
        if (bl0942_app.stages[last].enter_ms != 0) {
            uint32_t now = (uint32_t)get_time().val;
            bl0942_app.stages[last].elapsed_ms = now - bl0942_app.stages[last].enter_ms;
        }
    }

    clear_timeout();
    bl0942_app.task_state = next_state;

    if (next_state < BL0942_APP_STATE_COUNT) {
        bl0942_app.stages[next_state].enter_ms  = (uint32_t)get_time().val;
        bl0942_app.stages[next_state].elapsed_ms = 0;
    }

    bl0942_app.sub_step = 0;

    LOG_I("%s -> %s", bl0942_state_names[last], bl0942_state_names[next_state]);

    if (bl0942_app.notify_handler) {
        bl0942_app.notify_handler((uint8_t)next_state, NULL);
    }
}

/* ==================== 各阶段处理函数 ==================== */

/**
 * @brief 阶段 0: 初始化 (INIT)
 *
 * 步骤:
 *   0. GPIO 引脚初始化
 *   1. 设置总线结构体
 *   2. 调用 bl0942_init()
 */
static void process_state_init(void)
{
    bl0942_app.sub_step++;

    switch (bl0942_app.sub_step) {

    case 1:
        LOG_I("GPIO 初始化...");

        /* SEL (PB8) — 推挽输出, 低电平 = UART 模式 */
        {
            GPIO_InitTypeDef g;
            g.GPIO_Pin   = GPIO_Pin_8;
            g.GPIO_Mode  = GPIO_Mode_Out_PP;
            g.GPIO_Speed = GPIO_Speed_50MHz;
            GPIO_Init(GPIOB, &g);
            GPIO_WriteBit(GPIOB, GPIO_Pin_8, 0);  /* UART 模式 */
        }

        /* TX/SDO (PB9) — 推挽输出, 默认高 (UART 空闲) */
        {
            GPIO_InitTypeDef g;
            g.GPIO_Pin   = GPIO_Pin_9;
            g.GPIO_Mode  = GPIO_Mode_Out_PP;
            g.GPIO_Speed = GPIO_Speed_50MHz;
            GPIO_Init(GPIOB, &g);
            GPIO_WriteBit(GPIOB, GPIO_Pin_9, 1);
        }

        /* RX/SDI (PB10) — 上拉输入 */
        {
            GPIO_InitTypeDef g;
            g.GPIO_Pin   = GPIO_Pin_10;
            g.GPIO_Mode  = GPIO_Mode_IPU;
            g.GPIO_Speed = GPIO_Speed_50MHz;
            GPIO_Init(GPIOB, &g);
        }

        /* SCLK_BPS (PB11) — 推挽输出, 高电平 = 9600bps (UART) */
        {
            GPIO_InitTypeDef g;
            g.GPIO_Pin   = GPIO_Pin_11;
            g.GPIO_Mode  = GPIO_Mode_Out_PP;
            g.GPIO_Speed = GPIO_Speed_50MHz;
            GPIO_Init(GPIOB, &g);
            GPIO_WriteBit(GPIOB, GPIO_Pin_11, 1);  /* 9600bps */
        }

        /* CF1 (PA0) — 上拉输入 (可选, 用于检测脉冲输出) */
        {
            GPIO_InitTypeDef g;
            g.GPIO_Pin   = GPIO_Pin_0;
            g.GPIO_Mode  = GPIO_Mode_IPU;
            g.GPIO_Speed = GPIO_Speed_50MHz;
            GPIO_Init(GPIOA, &g);
        }
        break;

    case 2:
        LOG_I("配置总线...");

        /* 填充 bus 结构体 */
        bl0942_app.bus.interface              = BL0942_IF_UART;
        bl0942_app.bus.uart_tx                = bl0942_uart_tx_byte;
        bl0942_app.bus.uart_rx                = bl0942_uart_rx_byte;
        bl0942_app.bus.delay_us               = Delay_Us;
        bl0942_app.bus.uart_addr              = 0;       /* 单芯片地址 0 */
        bl0942_app.bus.uart_byte_timeout_us   = 20000;   /* 20ms 帧超时 */
        bl0942_app.bus.uart_read_delay_us     = 200;     /* 等待芯片返回 */

        if (bl0942_init(&bl0942_app.bus) != BL0942_OK) {
            LOG_E("bl0942_init 失败");
            set_state(BL0942_APP_STATE_ERROR);
            return;
        }
        bl0942_app.result.init_ok = 1;
        LOG_I("BL0942 驱动初始化成功");
        break;

    case 3:
        LOG_I("初始化完成");
        set_state(BL0942_APP_STATE_CHECK);
        break;
    }
}

/**
 * @brief 阶段 1: 器件检测 (CHECK)
 *
 * 步骤:
 *   0. Ping 通信测试
 *   1. 读取 STATUS 寄存器
 */
static void process_state_check(void)
{
    int8_t ret;
    uint16_t status;

    bl0942_app.sub_step++;

    switch (bl0942_app.sub_step) {

    case 1:
        LOG_I("通信检测 (Ping)...");
        ret = bl0942_ping(&bl0942_app.bus);
        if (ret != BL0942_OK) {
            LOG_E("Ping 失败 (ret=%d), 检查接线和供电", ret);
            set_state(BL0942_APP_STATE_ERROR);
            return;
        }
        bl0942_app.result.ping_ok = 1;
        LOG_I("Ping 成功, BL0942 响应正常");
        break;

    case 2:
        ret = bl0942_read_status(&bl0942_app.bus, &status);
        if (ret != BL0942_OK) {
            LOG_E("读取 STATUS 失败 (ret=%d)", ret);
            set_state(BL0942_APP_STATE_ERROR);
            return;
        }
        LOG_I("STATUS = 0x%03X", status);
        break;

    case 3:
        LOG_I("器件检测完成");
        set_state(BL0942_APP_STATE_CONFIGURE);
        break;
    }
}

/**
 * @brief 阶段 2: 参数配置 (CONFIGURE)
 *
 * 步骤:
 *   0. 解锁写保护
 *   1. 设置电流增益
 *   2. 设置过流阈值
 *   3. 设置 MODE 寄存器
 *   4. 设置输出功能
 *   5. 设置刷新周期
 *   6. 配置完成
 */
static void process_state_configure(void)
{
    int8_t ret;
    uint16_t mode_val;

    bl0942_app.sub_step++;

    switch (bl0942_app.sub_step) {

    case 1:
        LOG_I("解锁写保护...");
        ret = bl0942_unlock_wrprot(&bl0942_app.bus);
        if (ret != BL0942_OK) {
            LOG_E("解锁写保护失败 (ret=%d)", ret);
            set_state(BL0942_APP_STATE_ERROR);
            return;
        }
        LOG_I("写保护已解锁");
        break;

    case 2:
        LOG_I("设置电流增益 = %d...", bl0942_app.config.current_gain);
        ret = bl0942_set_gain(&bl0942_app.bus, bl0942_app.config.current_gain);
        if (ret != BL0942_OK) {
            LOG_E("设置增益失败 (ret=%d)", ret);
            set_state(BL0942_APP_STATE_ERROR);
            return;
        }
        break;

    case 3:
        LOG_I("设置过流阈值 = 0x%04X...", bl0942_app.config.overcurrent_threshold);
        ret = bl0942_set_overcurrent_th(&bl0942_app.bus,
                                         bl0942_app.config.overcurrent_threshold);
        if (ret != BL0942_OK) {
            LOG_E("设置过流阈值失败 (ret=%d)", ret);
            set_state(BL0942_APP_STATE_ERROR);
            return;
        }
        break;

    case 4:
        /* 构建 MODE 值 */
        mode_val = 0
            | (1 << 2)   /* CF_EN = 1 */
            | (bl0942_app.config.rms_update_400ms ? (1 << 3) : 0)
            | (bl0942_app.config.ac_freq_hz == 60 ? (1 << 5) : 0)
            | (1 << 7);  /* CF_CNT_ADD_SEL = 1 (绝对值累加) */

        LOG_I("设置 MODE = 0x%03X...", mode_val);
        ret = bl0942_set_mode(&bl0942_app.bus, mode_val);
        if (ret != BL0942_OK) {
            LOG_E("设置 MODE 失败 (ret=%d)", ret);
            set_state(BL0942_APP_STATE_ERROR);
            return;
        }
        break;

    case 5:
        LOG_I("设置输出功能 CF1=%d CF2=%d ZX=%d...",
              bl0942_app.config.cf1_func,
              bl0942_app.config.cf2_func,
              bl0942_app.config.zx_func);
        ret = bl0942_set_output(&bl0942_app.bus,
                                bl0942_app.config.cf1_func,
                                bl0942_app.config.cf2_func,
                                bl0942_app.config.zx_func);
        if (ret != BL0942_OK) {
            LOG_E("设置输出功能失败 (ret=%d)", ret);
            set_state(BL0942_APP_STATE_ERROR);
            return;
        }
        break;

    case 6:
        LOG_I("设置 I_FAST_RMS 刷新周期 = %d...", bl0942_app.config.fast_rms_cycle);
        ret = bl0942_write_reg(&bl0942_app.bus,
                                BL0942_REG_I_FAST_RMS_CYC,
                                bl0942_app.config.fast_rms_cycle);
        if (ret != BL0942_OK) LOG_W("设置快速 RMS 周期失败");

        LOG_I("设置 FREQ 刷新周期 = %d...", bl0942_app.config.freq_cycle);
        ret = bl0942_write_reg(&bl0942_app.bus,
                                BL0942_REG_FREQ_CYC,
                                bl0942_app.config.freq_cycle);
        if (ret != BL0942_OK) LOG_W("设置频率周期失败");
        break;

    case 7:
        bl0942_app.result.config_ok = 1;
        LOG_I("配置完成, 进入测量模式");
        bl0942_app.last_measure_ms = (uint32_t)get_time().val;
        bl0942_app.last_print_ms   = bl0942_app.last_measure_ms;
        set_state(BL0942_APP_STATE_MEASURE);
        break;
    }
}

/**
 * @brief 打印电参数
 */
static void print_measurement(const bl0942_measurement_t *m)
{
    LOG_I("══════════ BL0942 电参数 ══════════");
    LOG_I("  电压有效值:  %.2f V",     m->v_rms_v);
    LOG_I("  电流有效值:  %.4f A",    m->i_rms_a);
    LOG_I("  快速电流:    %.4f A",    m->i_fast_rms_a);
    LOG_I("  有功功率:    %.2f W",     m->watt_w);
    LOG_I("  频率:        %.2f Hz",   m->freq_hz);
    LOG_I("  电能脉冲:    %lu",       m->cf_cnt);
    LOG_I("  状态:        0x%03X",    m->status);
    LOG_I("    CF_REVP:    %d",       (m->status >> 0) & 1);
    LOG_I("    CREEP:      %d",       (m->status >> 1) & 1);
    LOG_I("    I_ZX_LTH:   %d",       (m->status >> 8) & 1);
    LOG_I("    V_ZX_LTH:   %d",       (m->status >> 9) & 1);
    LOG_I("══════════════════════════════════");
}

/**
 * @brief 阶段 3: 循环测量 (MEASURE)
 *
 * 周期性读取电参数，检测过流/反向功率状态。
 */
static void process_state_measure(void)
{
    int8_t  ret;
    uint32_t now = (uint32_t)get_time().val;

    /* 按测量间隔读取 */
    if (now - bl0942_app.last_measure_ms < bl0942_app.measure_interval_ms) {
        return;
    }
    bl0942_app.last_measure_ms = now;

    /* 读取全部电参数 */
    ret = bl0942_read_all(&bl0942_app.bus, &bl0942_app.meas);
    if (ret != BL0942_OK) {
        LOG_E("读取电参数失败 (ret=%d)", ret);
        /* 连续失败处理 */
        static uint8_t fail_cnt = 0;
        if (++fail_cnt >= 5) {
            LOG_E("连续读取失败, 进入错误状态");
            set_state(BL0942_APP_STATE_ERROR);
        }
        return;
    }
    fail_cnt = 0;  /* 成功则清零 */
    bl0942_app.result.measure_ok = 1;

    /* 检测反向功率 */
    if (bl0942_app.meas.status & BL0942_STATUS_CF_REVP_F_Msk) {
        bl0942_app.result.reverse_power = 1;
    } else {
        bl0942_app.result.reverse_power = 0;
    }

    /* 检测防潜动 */
    if (bl0942_app.meas.status & BL0942_STATUS_CREEP_F_Msk) {
        bl0942_app.result.creep_active = 1;
    } else {
        bl0942_app.result.creep_active = 0;
    }

    /* 检测过流 (I_FAST_RMS[23:8] 与阈值比较, 芯片硬件处理) */
    if (bl0942_app.meas.status & BL0942_STATUS_I_ZX_LTH_F_Msk) {
        /* I_RMS 高 6bit = 0 表示电流低于阈值, 反之可能过流 */
        /* 此处用 I_FAST_RMS 软件检测作为补充 */
    }
    if (bl0942_app.meas.i_fast_rms_a > 10.0f) {  /* 示例: 10A 过流 */
        if (++bl0942_app.overcurrent_cnt >= bl0942_app.overcurrent_threshold_cnt) {
            bl0942_app.result.overcurrent = 1;
            LOG_W("过流告警! I_FAST = %.4f A", bl0942_app.meas.i_fast_rms_a);
            set_state(BL0942_APP_STATE_OVERCURRENT);
            return;
        }
    } else {
        bl0942_app.overcurrent_cnt = 0;
    }

    /* 按打印间隔输出 */
    if (now - bl0942_app.last_print_ms >= bl0942_app.print_interval_ms) {
        print_measurement(&bl0942_app.meas);
        bl0942_app.last_print_ms = now;
    }
}

/**
 * @brief 阶段 4: 过流处理 (OVERCURRENT)
 */
static void process_state_overcurrent(void)
{
    LOG_W("过流状态持续中... 电流: %.4f A", bl0942_app.meas.i_fast_rms_a);

    /* 检测过流是否恢复 */
    if (bl0942_app.meas.i_fast_rms_a < 9.0f) {  /* 滞后 1A */
        bl0942_app.result.overcurrent = 0;
        bl0942_app.overcurrent_cnt    = 0;
        LOG_I("过流恢复, 返回测量模式");
        set_state(BL0942_APP_STATE_MEASURE);
        return;
    }

    /* 用户可在此添加保护动作: 断开继电器、声光告警等 */
    if (bl0942_app.notify_handler) {
        bl0942_app.notify_handler(0x80, NULL);  /* 事件 0x80 = 过流 */
    }
}

/**
 * @brief 阶段 5: 寄存器转储 (DUMP)
 */
static void process_state_dump(void)
{
    int8_t ret;
    uint32_t val;

    bl0942_app.sub_step++;

    /* 电参量寄存器 */
    struct {
        uint8_t  reg;
        const char *name;
    } regs[] = {
        { BL0942_REG_I_RMS,      "I_RMS"      },
        { BL0942_REG_V_RMS,      "V_RMS"      },
        { BL0942_REG_I_FAST_RMS, "I_FAST_RMS" },
        { BL0942_REG_WATT,       "WATT"       },
        { BL0942_REG_CF_CNT,     "CF_CNT"     },
        { BL0942_REG_FREQ,       "FREQ"       },
        { BL0942_REG_STATUS,     "STATUS"     },
        { BL0942_REG_GAIN_CR,    "GAIN_CR"    },
        { BL0942_REG_MODE,       "MODE"       },
        { BL0942_REG_OT_FUNX,    "OT_FUNX"    },
    };

    LOG_I("══════════ 寄存器转储 ══════════");

    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        ret = bl0942_read_reg(&bl0942_app.bus, regs[i].reg, &val);
        if (ret == BL0942_OK) {
            LOG_I("  [0x%02X] %-12s = 0x%06lX", regs[i].reg, regs[i].name, val);
        } else {
            LOG_W("  [0x%02X] %-12s = 读取失败 (ret=%d)", regs[i].reg, regs[i].name, ret);
        }
    }
    LOG_I("══════════════════════════════");

    set_state(BL0942_APP_STATE_IDLE);
}

/* ==================== 公开接口 ==================== */

void bl0942_app_init(void)
{
    memset(&bl0942_app, 0, sizeof(bl0942_app));

    /* 加载默认配置 */
    memcpy(&bl0942_app.config, &default_config, sizeof(default_config));

    /* 设置默认间隔 */
    bl0942_app.measure_interval_ms     = 1000;
    bl0942_app.print_interval_ms       = 5000;
    bl0942_app.overcurrent_threshold_cnt = 3;

    bl0942_app.task_state = BL0942_APP_STATE_INIT;
    bl0942_app.last_state = BL0942_APP_STATE_INIT;

    LOG_I("BL0942 应用初始化完成，状态机启动");
}

void bl0942_app_process(void)
{
    bl0942_app_state_t state = bl0942_app.task_state;

    /* 状态切换同步 */
    if (bl0942_app.last_state != state) {
        /* 日志已在 set_state 中处理 */
    }
    bl0942_app.last_state = state;

    /* 阶段分发 */
    switch (state) {

    case BL0942_APP_STATE_INIT:
        process_state_init();
        break;

    case BL0942_APP_STATE_CHECK:
        process_state_check();
        break;

    case BL0942_APP_STATE_CONFIGURE:
        process_state_configure();
        break;

    case BL0942_APP_STATE_MEASURE:
        process_state_measure();
        break;

    case BL0942_APP_STATE_OVERCURRENT:
        process_state_overcurrent();
        break;

    case BL0942_APP_STATE_DUMP:
        process_state_dump();
        break;

    case BL0942_APP_STATE_IDLE:
        /* 空闲，可在此添加低功耗逻辑 */
        break;

    case BL0942_APP_STATE_ERROR:
        if (bl0942_app.last_state != BL0942_APP_STATE_ERROR) {
            LOG_E("致命错误，状态机停止。请检查硬件连接和供电。");
        }
        break;

    default:
        break;
    }

    /* 超时监控 */
    if (bl0942_app.timeout) {
        if (get_time().val >= bl0942_app.timeout) {
            LOG_W("状态超时! 跳转至 %s", bl0942_state_names[bl0942_app.timeout_state]);
            set_state(bl0942_app.timeout_state);
        }
    }
}

const bl0942_measurement_t *bl0942_app_get_measurement(void)
{
    return &bl0942_app.meas;
}

void bl0942_app_set_interval(uint32_t measure_ms, uint32_t print_ms)
{
    if (measure_ms > 0) {
        bl0942_app.measure_interval_ms = measure_ms;
    }
    if (print_ms > 0) {
        bl0942_app.print_interval_ms = print_ms;
    }
}

void bl0942_app_set_notify(bl0942_app_notify_t handler)
{
    bl0942_app.notify_handler = handler;
}
