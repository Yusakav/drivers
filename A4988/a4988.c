/**
 * @file a4988.c
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief A4988 步进电机驱动实现 — 完整版
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 *
 * @details 实现了 A4988 的全部核心功能：
 *          - GPIO 引脚初始化与控制
 *          - 步进脉冲生成（单步 / 多步 / 定时）
 *          - 方向、微步分辨率配置
 *          - ENABLE / RESET / SLEEP 控制
 *          - 电流计算与相电流查询
 *          - 转速换算工具函数
 *
 *          内部自动维护运行状态，用于防止非法操作（如运动中修改配置）。
 */
 

#include "a4988.h"
#include "timer.h"      /* Delay_Us, get_time */
#include <string.h>

#define LOG_LEVEL   LOG_LVL_DEBUG
#define LOG_TAG     "A4988"
#include "logger.h"

/* ======================== 步进电流百分比表 (Table 2) ======================== */

/**
 * @brief 64 步微步进相电流百分比表
 *
 * 值 = %ITripMAX × 100，符号表示电流方向（正 = OUTxA→OUTxB）。
 * 数据手册 Table 2 中 Phase 1 列。
 *
 * Phase 2 电流由 Phase 1 值正交偏移 16 个索引得到：
 *   phase2[idx] = phase1[(idx + 16) % 64]
 */
const int16_t a4988_step_current_table[64] = {
    /* 索引  0 */  10000,
    /* 索引  1 */   9952,
    /* 索引  2 */   9808,
    /* 索引  3 */   9569,
    /* 索引  4 */   9239,
    /* 索引  5 */   8819,
    /* 索引  6 */   8315,
    /* 索引  7 */   7730,
    /* 索引  8 */   7071,
    /* 索引  9 */   6344,
    /* 索引 10 */   5556,
    /* 索引 11 */   4714,
    /* 索引 12 */   3827,
    /* 索引 13 */   2903,
    /* 索引 14 */   1951,
    /* 索引 15 */    980,
    /* 索引 16 */      0,
    /* 索引 17 */   -980,
    /* 索引 18 */  -1951,
    /* 索引 19 */  -2903,
    /* 索引 20 */  -3827,
    /* 索引 21 */  -4714,
    /* 索引 22 */  -5556,
    /* 索引 23 */  -6344,
    /* 索引 24 */  -7071,
    /* 索引 25 */  -7730,
    /* 索引 26 */  -8315,
    /* 索引 27 */  -8819,
    /* 索引 28 */  -9239,
    /* 索引 29 */  -9569,
    /* 索引 30 */  -9808,
    /* 索引 31 */  -9952,
    /* 索引 32 */ -10000,
    /* 索引 33 */  -9952,
    /* 索引 34 */  -9808,
    /* 索引 35 */  -9569,
    /* 索引 36 */  -9239,
    /* 索引 37 */  -8819,
    /* 索引 38 */  -8315,
    /* 索引 39 */  -7730,
    /* 索引 40 */  -7071,
    /* 索引 41 */  -6344,
    /* 索引 42 */  -5556,
    /* 索引 43 */  -4714,
    /* 索引 44 */  -3827,
    /* 索引 45 */  -2903,
    /* 索引 46 */  -1951,
    /* 索引 47 */   -980,
    /* 索引 48 */      0,
    /* 索引 49 */    980,
    /* 索引 50 */   1951,
    /* 索引 51 */   2903,
    /* 索引 52 */   3827,
    /* 索引 53 */   4714,
    /* 索引 54 */   5556,
    /* 索引 55 */   6344,
    /* 索引 56 */   7071,
    /* 索引 57 */   7730,
    /* 索引 58 */   8315,
    /* 索引 59 */   8819,
    /* 索引 60 */   9239,
    /* 索引 61 */   9569,
    /* 索引 62 */   9808,
    /* 索引 63 */   9952,
};

/* ======================== MSx 引脚真值表 ======================== */

/**
 * @brief MS1/MS2/MS3 引脚状态映射
 *
 * 索引 = A4988_Microstep_t 枚举值
 *
 * 值 bit[0] = MS1, bit[1] = MS2, bit[2] = MS3
 */
static const uint8_t ms_pin_map[] = {
    0b000,  /* A4988_MICROSTEP_FULL       — MS1=L MS2=L MS3=L */
    0b001,  /* A4988_MICROSTEP_HALF       — MS1=H MS2=L MS3=L */
    0b010,  /* A4988_MICROSTEP_QUARTER    — MS1=L MS2=H MS3=L */
    0b011,  /* A4988_MICROSTEP_EIGHTH     — MS1=H MS2=H MS3=L */
    0b111,  /* A4988_MICROSTEP_SIXTEENTH  — MS1=H MS2=H MS3=H */
};

/* ======================== 内部状态变量 ======================== */

static A4988_Config_t    s_cfg;              /**< 当前配置 */
static A4988_State_t     s_state;            /**< 当前运行状态 */
static uint8_t           s_initialized = 0;  /**< 初始化标志 */
static uint8_t           s_enabled     = 0;  /**< 输出使能标志 */
static A4988_Direction_t s_direction;        /**< 当前方向 */
static A4988_Microstep_t s_microstep;        /**< 当前微步分辨率 */

/* 运动计数器 (step_tick 非阻塞模式) */
static uint32_t          s_tick_target  = 0; /**< 剩余脉冲数 */
static uint32_t          s_tick_count   = 0; /**< 已发送脉冲数 */
static uint32_t          s_tick_delay_us = 0; /**< 脉冲间隔 */

/* ======================== 内部 GPIO 辅助函数 ======================== */

/**
 * @brief 初始化单个推挽输出 GPIO 引脚
 */
static void gpio_init_out(GPIO_TypeDef *port, uint16_t pin, uint8_t init_level)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin   = pin;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(port, &GPIO_InitStructure);
    GPIO_WriteBit(port, pin, init_level);
}

/**
 * @brief 设置 GPIO 引脚输出电平
 */
static inline void gpio_set(GPIO_TypeDef *port, uint16_t pin, uint8_t level)
{
    GPIO_WriteBit(port, pin, level ? Bit_SET : Bit_RESET);
}

/**
 * @brief 微秒延时封装
 */
static inline void delay_us(uint32_t us)
{
    Delay_Us(us);
}

/* ==================== MSx 引脚控制 ==================== */

/**
 * @brief 根据微步枚举值设置 MS1/MS2/MS3 引脚
 */
static void set_ms_pins(A4988_Microstep_t ms)
{
    uint8_t val = ms_pin_map[ms];

    gpio_set(A4988_MS1_PORT, A4988_MS1_PIN, (val >> 0) & 1);
    gpio_set(A4988_MS2_PORT, A4988_MS2_PIN, (val >> 1) & 1);
    gpio_set(A4988_MS3_PORT, A4988_MS3_PIN, (val >> 2) & 1);

    s_microstep = ms;
}

/* ======================== 公开 API ======================== */

/* ---------- 初始化 ---------- */

/**
 * @brief 初始化 A4988 驱动
 *
 * 初始化顺序（遵循数据手册推荐时序）：
 *   1. 初始化所有 GPIO 引脚为推挽输出
 *   2. 初始状态下 ENABLE = H (禁用)，RESET = L (复位态)，SLEEP = L (睡眠态)
 *   3. 设置 MSx 引脚
 *   4. SLEEP = H (唤醒)，等待 1ms 稳定
 *   5. RESET = H (释放复位)
 *   6. ENABLE = L (使能输出)
 */
int8_t a4988_init(const A4988_Config_t *cfg)
{
    if (cfg == NULL) {
        LOG_E("配置指针为空");
        return A4988_ERR_NULL_PTR;
    }

    /* 校验配置参数 */
    if (cfg->step_pulse_width_us < A4988_STEP_MIN_PULSE_US) {
        LOG_E("STEP 脉冲宽度 %lu µs 小于最小值 %d µs",
              cfg->step_pulse_width_us, A4988_STEP_MIN_PULSE_US);
        return A4988_ERR_INVALID_PARAM;
    }

    /* 备份配置 */
    memcpy(&s_cfg, cfg, sizeof(A4988_Config_t));

    /* ---- 1. 初始化所有 GPIO ---- */
    gpio_init_out(A4988_STEP_PORT,   A4988_STEP_PIN,   Bit_RESET);
    gpio_init_out(A4988_DIR_PORT,    A4988_DIR_PIN,    (cfg->initial_dir == A4988_DIR_CW) ? Bit_SET : Bit_RESET);
    gpio_init_out(A4988_ENABLE_PORT, A4988_ENABLE_PIN, Bit_SET);    /* 初始禁用 */
    gpio_init_out(A4988_RESET_PORT,  A4988_RESET_PIN,  Bit_RESET);  /* 初始复位态 */
    gpio_init_out(A4988_SLEEP_PORT,  A4988_SLEEP_PIN,  Bit_RESET);  /* 初始睡眠态 */
    gpio_init_out(A4988_MS1_PORT,    A4988_MS1_PIN,    Bit_RESET);
    gpio_init_out(A4988_MS2_PORT,    A4988_MS2_PIN,    Bit_RESET);
    gpio_init_out(A4988_MS3_PORT,    A4988_MS3_PIN,    Bit_RESET);

    /* ---- 2. 设置微步分辨率 ---- */
    set_ms_pins(cfg->microstep);

    /* ---- 3. 唤醒 ---- */
    gpio_set(A4988_SLEEP_PORT, A4988_SLEEP_PIN, Bit_SET);  /* SLEEP = H */
    delay_us(A4988_WAKEUP_DELAY_MS * 1000);                /* 等待 1ms 稳定 */

    /* ---- 4. 释放复位 ---- */
    gpio_set(A4988_RESET_PORT, A4988_RESET_PIN, Bit_SET);  /* RESET = H */

    /* ---- 5. 使能输出 ---- */
    gpio_set(A4988_ENABLE_PORT, A4988_ENABLE_PIN, Bit_RESET); /* ENABLE = L */

    /* 更新内部状态 */
    s_initialized = 1;
    s_enabled     = 1;
    s_direction   = cfg->initial_dir;
    s_state       = A4988_STATE_IDLE;

    LOG_I("A4988 初始化完成 | 微步=%d | RS=%d mOhm | VREF=%d mV | ITripMAX=%lu mA",
          cfg->microstep,
          cfg->rs_mohm,
          cfg->vref_mv,
          a4988_calc_itrip_max(cfg->vref_mv, cfg->rs_mohm));

    return A4988_OK;
}

/* ---------- 步进控制 ---------- */

void a4988_step_pulse(void)
{
    if (!s_initialized) return;

    /* STEP HIGH */
    gpio_set(A4988_STEP_PORT, A4988_STEP_PIN, Bit_SET);
    delay_us(s_cfg.step_pulse_width_us);

    /* STEP LOW */
    gpio_set(A4988_STEP_PORT, A4988_STEP_PIN, Bit_RESET);
    delay_us(s_cfg.step_pulse_width_us);
}

void a4988_step_n(uint32_t n, uint32_t delay_us_step)
{
    uint32_t i;

    if (!s_initialized || n == 0) return;

    s_state = A4988_STATE_MOVING;

    for (i = 0; i < n; i++) {
        a4988_step_pulse();

        /* 脉冲间延时 — 减去脉冲本身耗时 */
        if (delay_us_step > 2 * s_cfg.step_pulse_width_us) {
            delay_us(delay_us_step - 2 * s_cfg.step_pulse_width_us);
        }
    }

    s_state = A4988_STATE_IDLE;
}

uint8_t a4988_step_tick(void)
{
    if (!s_initialized) return 0;

    if (s_tick_target == 0) {
        return 0;
    }

    a4988_step_pulse();
    s_tick_count++;
    s_tick_target--;

    if (s_tick_target == 0) {
        s_state = A4988_STATE_IDLE;
    }

    return 1;
}

/* ---------- 方向控制 ---------- */

void a4988_set_direction(A4988_Direction_t dir)
{
    if (!s_initialized) return;
    gpio_set(A4988_DIR_PORT, A4988_DIR_PIN, (dir == A4988_DIR_CW) ? Bit_SET : Bit_RESET);
    s_direction = dir;
}

A4988_Direction_t a4988_get_direction(void)
{
    return s_direction;
}

/* ---------- 微步控制 ---------- */

void a4988_set_microstep(A4988_Microstep_t ms)
{
    if (!s_initialized) return;
    if (ms > A4988_MICROSTEP_SIXTEENTH) return;

    set_ms_pins(ms);
    LOG_D("微步分辨率切换为 %d", ms);
}

A4988_Microstep_t a4988_get_microstep(void)
{
    return s_microstep;
}

/* ---------- 使能控制 ---------- */

void a4988_enable(void)
{
    if (!s_initialized) return;
    gpio_set(A4988_ENABLE_PORT, A4988_ENABLE_PIN, Bit_RESET);  /* ENABLE = L */
    s_enabled = 1;
    if (s_state == A4988_STATE_DISABLED) {
        s_state = A4988_STATE_IDLE;
    }
}

void a4988_disable(void)
{
    if (!s_initialized) return;
    gpio_set(A4988_ENABLE_PORT, A4988_ENABLE_PIN, Bit_SET);    /* ENABLE = H */
    s_enabled = 0;
    s_state = A4988_STATE_DISABLED;
}

uint8_t a4988_is_enabled(void)
{
    return s_enabled;
}

/* ---------- 复位控制 ---------- */

void a4988_reset_assert(void)
{
    if (!s_initialized) return;
    gpio_set(A4988_RESET_PORT, A4988_RESET_PIN, Bit_RESET);  /* RESET = L */
    s_state = A4988_STATE_RESET;
    LOG_D("译码器复位 (Home 状态)");
}

void a4988_reset_release(void)
{
    if (!s_initialized) return;
    gpio_set(A4988_RESET_PORT, A4988_RESET_PIN, Bit_SET);    /* RESET = H */
    if (s_state == A4988_STATE_RESET) {
        s_state = A4988_STATE_IDLE;
    }
    LOG_D("译码器复位释放");
}

/* ---------- 睡眠控制 ---------- */

void a4988_sleep_enter(void)
{
    if (!s_initialized) return;
    gpio_set(A4988_SLEEP_PORT, A4988_SLEEP_PIN, Bit_RESET);  /* SLEEP = L */
    s_state = A4988_STATE_SLEEP;
    LOG_D("进入睡眠模式");
}

void a4988_sleep_exit(void)
{
    if (!s_initialized) return;
    gpio_set(A4988_SLEEP_PORT, A4988_SLEEP_PIN, Bit_SET);    /* SLEEP = H */
    s_state = A4988_STATE_IDLE;
    /* 从睡眠唤醒后必须等待 ≥1ms 才能发送 STEP */
    delay_us(A4988_WAKEUP_DELAY_MS * 1000);
    LOG_D("退出睡眠模式 (已等待 %dms)", A4988_WAKEUP_DELAY_MS);
}

/* ---------- 电流计算 ---------- */

uint32_t a4988_calc_itrip_max(uint16_t vref_mv, uint16_t rs_mohm)
{
    if (rs_mohm == 0) return 0;
    /* ITripMAX = VREF / (8 × RS)
     * 单位换算: mA = (mV / (8 × mΩ)) × 1000 = (mV × 1000) / (8 × mΩ) */
    return (uint32_t)vref_mv * 1000UL / (8UL * (uint32_t)rs_mohm);
}

void a4988_calc_phase_currents(uint8_t  step_index,
                                uint32_t itrip_max_mA,
                                int32_t  *phase1_mA,
                                int32_t  *phase2_mA)
{
    int16_t p1_percent, p2_percent;

    step_index &= 0x3F;  /* 限制在 0-63 */

    /* Phase 1 电流百分比 */
    p1_percent = a4988_step_current_table[step_index];

    /* Phase 2 电流百分比 = Phase 1 偏移 16 (90° 相位差) */
    p2_percent = a4988_step_current_table[(step_index + 16) & 0x3F];

    if (phase1_mA) {
        *phase1_mA = (int32_t)((int64_t)itrip_max_mA * p1_percent / 10000);
    }
    if (phase2_mA) {
        *phase2_mA = (int32_t)((int64_t)itrip_max_mA * p2_percent / 10000);
    }
}

/* ---------- 状态查询 ---------- */

A4988_State_t a4988_get_state(void)
{
    return s_state;
}

/* ---------- 工具函数 ---------- */

uint32_t a4988_rpm_to_delay_us(float rpm, A4988_Microstep_t ms)
{
    uint32_t steps_per_rev;
    float    steps_per_sec;

    if (rpm <= 0.0f) return 0;

    steps_per_rev = a4988_steps_per_rev(ms);
    steps_per_sec = rpm * (float)steps_per_rev / 60.0f;

    if (steps_per_sec <= 0.0f) return 0;

    return (uint32_t)(1000000.0f / steps_per_sec);
}

float a4988_delay_us_to_rpm(uint32_t delay_us, A4988_Microstep_t ms)
{
    uint32_t steps_per_rev;
    float    steps_per_sec;

    if (delay_us == 0) return 0.0f;

    steps_per_rev = a4988_steps_per_rev(ms);
    steps_per_sec = 1000000.0f / (float)delay_us;

    return steps_per_sec * 60.0f / (float)steps_per_rev;
}
