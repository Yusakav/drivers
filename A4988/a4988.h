/**
 * @file a4988.h
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief A4988 步进电机驱动 — 寄存器/位定义、数据结构、API 声明
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 *
 * @details A4988 是 Allegro 公司的 DMOS 微步进电机驱动芯片，内置译码器，
 *          支持全步 / 1/2 / 1/4 / 1/8 / 1/16 五种微步分辨率。
 *          本驱动基于 CH32X035 MCU GPIO 直接控制，无需 I2C/SPI 外设。
 *
 * @note  硬件引脚定义 (可通过下方宏重定义，适配不同 PCB)
 *    A4988_STEP          PA0    步进脉冲 (上升沿有效)
 *    A4988_DIR           PA1    方向控制
 *    A4988_ENABLE        PA2    使能输出 (低电平有效)
 *    A4988_RESET         PA3    复位译码器 (低电平有效)
 *    A4988_SLEEP         PA4    睡眠模式 (低电平有效)
 *    A4988_MS1           PB0    微步选择位 1
 *    A4988_MS2           PB1    微步选择位 2
 *    A4988_MS3           PB3    微步选择位 3
 */

#ifndef A4988_H
#define A4988_H

#include <stdint.h>
#include "ch32x035.h"

/* ======================== 位操作宏 ======================== */

#define _BIT(x)                 (1 << (x))

/* ======================== 硬件引脚定义 (可按需重定义) ======================== */

#ifndef A4988_STEP_PORT
#define A4988_STEP_PORT         GPIOA
#define A4988_STEP_PIN          GPIO_Pin_0
#endif

#ifndef A4988_DIR_PORT
#define A4988_DIR_PORT          GPIOA
#define A4988_DIR_PIN           GPIO_Pin_1
#endif

#ifndef A4988_ENABLE_PORT
#define A4988_ENABLE_PORT       GPIOA
#define A4988_ENABLE_PIN        GPIO_Pin_2
#endif

#ifndef A4988_RESET_PORT
#define A4988_RESET_PORT        GPIOA
#define A4988_RESET_PIN         GPIO_Pin_3
#endif

#ifndef A4988_SLEEP_PORT
#define A4988_SLEEP_PORT        GPIOA
#define A4988_SLEEP_PIN         GPIO_Pin_4
#endif

#ifndef A4988_MS1_PORT
#define A4988_MS1_PORT          GPIOB
#define A4988_MS1_PIN           GPIO_Pin_0
#endif

#ifndef A4988_MS2_PORT
#define A4988_MS2_PORT          GPIOB
#define A4988_MS2_PIN           GPIO_Pin_1
#endif

#ifndef A4988_MS3_PORT
#define A4988_MS3_PORT          GPIOB
#define A4988_MS3_PIN           GPIO_Pin_3
#endif

/* ======================== 电气参数宏 ======================== */

/** @brief VBB 供电电压范围 (V) */
#define A4988_VBB_MIN           8
#define A4988_VBB_MAX           35

/** @brief VDD 逻辑供电范围 (V) */
#define A4988_VDD_MIN           3.0f
#define A4988_VDD_MAX           5.5f

/** @brief 最大输出电流 (A) */
#define A4988_IOUT_MAX          2.0f

/** @brief VREF 输入范围 (V) */
#define A4988_VREF_MAX          4.0f

/** @brief SENSE 引脚最大电压 (V) */
#define A4988_SENSE_MAX_MV      500

/** @brief 默认固定关断时间 — ROSC 接 VDD 或 GND 时 (µs) */
#define A4988_TOFF_DEFAULT_US   30

/** @brief 消隐时间典型值 (µs) */
#define A4988_TBLANK_US         1

/** @brief 步进脉冲最小高/低电平宽度 (µs) */
#define A4988_STEP_MIN_PULSE_US 1

/** @brief MSx/DIR 对 STEP 的建立时间 (ns) */
#define A4988_SETUP_TIME_NS     200

/** @brief MSx/DIR 对 STEP 的保持时间 (ns) */
#define A4988_HOLD_TIME_NS      200

/** @brief 从 SLEEP 唤醒后需等待的稳定时间 (ms) */
#define A4988_WAKEUP_DELAY_MS   1

/* ======================== 电流计算宏 ======================== */

/**
 * @brief 计算最大跳变电流 ITripMAX
 *
 * ITripMAX = VREF / (8 × RS)
 *
 * @param vref_mV  VREF 参考电压 (mV)
 * @param rs_mOhm  检流电阻值 (mΩ)
 * @return ITripMAX (mA)
 */
#define A4988_CALC_ITRIP_MAX_MA(vref_mV, rs_mOhm)  \
    ((uint32_t)((uint32_t)(vref_mV) * 1000UL / (8UL * (uint32_t)(rs_mOhm))))

/**
 * @brief 根据百分比计算实际跳变电流
 *
 * ITrip = (percent / 100) × ITripMAX
 *
 * @param itrip_max_mA  ITripMAX (mA)
 * @param percent       电流百分比 (% × 100，如 70.71% 表示为 7071)
 * @return ITrip (mA)
 */
#define A4988_CALC_ITRIP_MA(itrip_max_mA, percent_x100) \
    ((uint32_t)((uint32_t)(itrip_max_mA) * (uint32_t)(percent_x100) / 10000UL))

/* ======================== 错误码定义 ======================== */

#define A4988_OK                    0   /**< 操作成功 */
#define A4988_ERR_NULL_PTR         -1   /**< 空指针错误 */
#define A4988_ERR_INVALID_PARAM    -2   /**< 参数无效（超出范围） */
#define A4988_ERR_NOT_INITIALIZED  -3   /**< 驱动未初始化 */
#define A4988_ERR_BUSY             -4   /**< 驱动正在执行运动，无法接受新命令 */
#define A4988_ERR_OVERCURRENT      -5   /**< 过流保护触发 */
#define A4988_ERR_THERMAL          -6   /**< 过热保护触发 */

/* ======================== 微步分辨率枚举 ======================== */

/**
 * @brief A4988 微步分辨率
 *
 * 对应 MS1/MS2/MS3 引脚状态 (见表 Table 1: Microstepping Resolution Truth Table)
 *
 * | MS1 | MS2 | MS3 | 分辨率      | 励磁模式  | 每圈步数 (1.8°电机) |
 * |-----|-----|-----|------------|-----------|--------------------|
 * |  L  |  L  |  L  | Full Step  | 2-Phase   | 200                |
 * |  H  |  L  |  L  | Half Step  | 1-2 Phase | 400                |
 * |  L  |  H  |  L  | 1/4 Step   | W1-2 Ph   | 800                |
 * |  H  |  H  |  L  | 1/8 Step   | 2W1-2 Ph  | 1600               |
 * |  H  |  H  |  H  | 1/16 Step  | 4W1-2 Ph  | 3200               |
 */
typedef enum {
    A4988_MICROSTEP_FULL       = 0,    /**< 全步 (MS1=L, MS2=L, MS3=L) */
    A4988_MICROSTEP_HALF       = 1,    /**< 半步 (MS1=H, MS2=L, MS3=L) */
    A4988_MICROSTEP_QUARTER    = 2,    /**< 1/4 步 (MS1=L, MS2=H, MS3=L) */
    A4988_MICROSTEP_EIGHTH     = 3,    /**< 1/8 步 (MS1=H, MS2=H, MS3=L) */
    A4988_MICROSTEP_SIXTEENTH  = 4,    /**< 1/16 步 (MS1=H, MS2=H, MS3=H) */
} A4988_Microstep_t;

/**
 * @brief 每种微步分辨率每圈的步数 (基于 1.8° 步距角电机)
 */
#define A4988_STEPS_PER_REV_FULL         200
#define A4988_STEPS_PER_REV_HALF         400
#define A4988_STEPS_PER_REV_QUARTER      800
#define A4988_STEPS_PER_REV_EIGHTH       1600
#define A4988_STEPS_PER_REV_SIXTEENTH    3200

/* ======================== 方向枚举 ======================== */

/**
 * @brief A4988 旋转方向
 */
typedef enum {
    A4988_DIR_CCW = 0,  /**< 逆时针 (Phase 2 current positive → Phase 1 positive) */
    A4988_DIR_CW  = 1,  /**< 顺时针 (Phase 1 current positive → Phase 2 positive) */
} A4988_Direction_t;

/* ======================== ROSC 模式枚举 ======================== */

/**
 * @brief ROSC 引脚配置模式
 *
 * 控制 PWM 关断时间 (tOFF) 和电流衰减模式。
 *
 * - A4988_ROSC_VDD: tOFF = 30µs, 自动 Mixed Decay (全步时为 Slow Decay)
 * - A4988_ROSC_GND: tOFF = 30µs, 全部 Mixed Decay (所有步进模式)
 * - A4988_ROSC_RESISTOR: tOFF ≈ ROSC / 825 µs, 自动 Mixed Decay
 */
typedef enum {
    A4988_ROSC_VDD       = 0,    /**< ROSC 接 VDD，自动衰减选择 */
    A4988_ROSC_GND       = 1,    /**< ROSC 接 GND，强制 Mixed Decay */
    A4988_ROSC_RESISTOR  = 2,    /**< ROSC 通过电阻接 GND，可编程关断时间 */
} A4988_ROSC_Mode_t;

/* ======================== 衰减模式枚举 ======================== */

/**
 * @brief 电流衰减模式 (用于状态监视和调试)
 *
 * A4988 内部自动选择衰减模式：
 *   - DAC 输出降低时 → Mixed Decay (快衰 31.25% tOFF + 慢衰余下)
 *   - DAC 输出升高或不变时 → Slow Decay
 *   - ROSC 接 GND → 始终 Mixed Decay
 */
typedef enum {
    A4988_DECAY_SLOW  = 0,  /**< 慢速衰减 (Slow Decay) */
    A4988_DECAY_MIXED = 1,  /**< 混合衰减 (Mixed Decay) — 快衰 31.25% + 慢衰余下 */
} A4988_DecayMode_t;

/* ======================== A4988 运行状态枚举 ======================== */

typedef enum {
    A4988_STATE_IDLE    = 0,    /**< 空闲，输出使能 */
    A4988_STATE_DISABLED = 1,   /**< 输出禁用 (ENABLE=H) */
    A4988_STATE_SLEEP   = 2,    /**< 睡眠模式 (SLEEP=L) */
    A4988_STATE_RESET   = 3,    /**< 复位状态 (RESET=L) */
    A4988_STATE_MOVING  = 4,    /**< 正在运动 */
    A4988_STATE_FAULT   = 5,    /**< 故障状态 (过温/过流) */
} A4988_State_t;

/* ======================== 步进电流百分比表 ======================== */

/**
 * @brief 微步每一步对应的相电流百分比 (% × 100)
 *
 * 数据来源于 A4988 数据手册 Table 2: Step Sequencing Settings。
 * 每个条目代表该步 Phase 1 电流百分比 (%ItripMAX × 100)。
 * Phase 2 电流 = 前一条目的 Phase 1 电流 (即正弦 / 余弦正交关系)。
 *
 * 数组索引与微步号对应：
 *   - Full step: 仅索引 0, 8, 16, 24 (4 步/电周期)
 *   - Half step: 索引 0, 2, 4, 6, 8, 10, 12, 14 (8 步/电周期)
 *   - 1/4 step:  索引 0, 1, 2, 3, ... 15 (16 步/电周期)
 *   - 1/8 step:  索引 0..31 (32 步/电周期)
 *   - 1/16 step: 索引 0..63 (64 步/电周期)
 *
 * 全周期 64 条目的 %ItripMAX × 100 值。
 * 值 10000 = 100.00%,  7071 = 70.71%,  3827 = 38.27%, 以此类推。
 */
extern const int16_t a4988_step_current_table[64];

/* ======================== 配置结构体 ======================== */

/**
 * @brief A4988 配置参数
 */
typedef struct {
    A4988_Microstep_t  microstep;        /**< 微步分辨率 */
    A4988_ROSC_Mode_t  rosc_mode;        /**< ROSC 引脚模式 */
    uint32_t           rosc_resistor_ohm;/**< ROSC 电阻值 (Ω)，仅 ROSC_RESISTOR 模式有效 */
    uint16_t           rs_mohm;          /**< 检流电阻值 (mΩ) */
    uint16_t           vref_mv;          /**< VREF 参考电压 (mV)，用于计算 ITripMAX */
    uint32_t           step_pulse_width_us; /**< STEP 脉冲宽度 (µs)，默认 2µs，最小值 1µs */
    A4988_Direction_t  initial_dir;      /**< 初始方向 */
} A4988_Config_t;

/* ======================== 默认配置宏 ======================== */

#define A4988_CONFIG_DEFAULT {                  \
    .microstep          = A4988_MICROSTEP_FULL,  \
    .rosc_mode          = A4988_ROSC_VDD,        \
    .rosc_resistor_ohm  = 0,                     \
    .rs_mohm            = 200,                   \
    .vref_mv            = 1600,                  \
    .step_pulse_width_us = 2,                    \
    .initial_dir        = A4988_DIR_CW,          \
}

/* ======================== 步进表查询 ======================== */

/**
 * @brief 根据微步分辨率获取电周期步数
 */
static inline uint8_t a4988_steps_per_electrical_cycle(A4988_Microstep_t ms)
{
    static const uint8_t steps[] = { 4, 8, 16, 32, 64 };
    return steps[ms];
}

/**
 * @brief 获取当前微步分辨率每圈的步数 (基于 1.8° 电机)
 */
static inline uint32_t a4988_steps_per_rev(A4988_Microstep_t ms)
{
    static const uint32_t steps[] = {
        A4988_STEPS_PER_REV_FULL,
        A4988_STEPS_PER_REV_HALF,
        A4988_STEPS_PER_REV_QUARTER,
        A4988_STEPS_PER_REV_EIGHTH,
        A4988_STEPS_PER_REV_SIXTEENTH,
    };
    return steps[ms];
}

/**
 * @brief 获取当前微步分辨率下每一步的角度 (度 × 1000)
 *
 * 如全步 = 1800 (即 1.8°)，1/16 步 = 112 (即 0.1125°)。
 */
static inline uint32_t a4988_step_angle_mdeg(A4988_Microstep_t ms)
{
    return 360000UL / a4988_steps_per_rev(ms);
}

/* ======================== API 函数声明 ======================== */

/* ---------- 初始化 ---------- */

/**
 * @brief 初始化 A4988 驱动
 *
 * 完成 GPIO 初始化，将芯片设置为已知状态 (RESET 释放、ENABLE 使能、SLEEP 唤醒)。
 *
 * @param cfg 配置参数指针 (不可为 NULL)
 * @return int8_t 状态码 A4988_OK 或错误码
 */
int8_t a4988_init(const A4988_Config_t *cfg);

/* ---------- 步进控制 ---------- */

/**
 * @brief 发送单个步进脉冲 (STEP 低→高→低)
 *
 * 脉冲宽度由 cfg->step_pulse_width_us 决定。
 * 阻塞执行，约耗时 2 × pulse_width。
 */
void a4988_step_pulse(void);

/**
 * @brief 发送 N 个步进脉冲
 *
 * 阻塞执行，脉冲间隔由 delay_us 参数控制。
 *
 * @param n        步进脉冲数量
 * @param delay_us 脉冲间延迟 (µs)，决定转速
 */
void a4988_step_n(uint32_t n, uint32_t delay_us);

/**
 * @brief 以指定频率连续旋转
 *
 * 非阻塞 — 发送一个脉冲后立即返回，需周期性调用以实现连续运动。
 * 调用频率应匹配目标步进频率。
 *
 * @return 实际发送的脉冲数 (0 或 1)
 */
uint8_t a4988_step_tick(void);

/* ---------- 方向控制 ---------- */

/**
 * @brief 设置旋转方向
 *
 * 方向变更在下一次 STEP 上升沿生效。
 *
 * @param dir 旋转方向
 */
void a4988_set_direction(A4988_Direction_t dir);

/**
 * @brief 获取当前方向
 */
A4988_Direction_t a4988_get_direction(void);

/* ---------- 微步控制 ---------- */

/**
 * @brief 设置微步分辨率
 *
 * 变更在下一次 STEP 上升沿生效。
 * 建议在变更前先 RESET 译码器，避免丢步。
 *
 * @param ms 微步分辨率
 */
void a4988_set_microstep(A4988_Microstep_t ms);

/**
 * @brief 获取当前微步分辨率
 */
A4988_Microstep_t a4988_get_microstep(void);

/* ---------- 使能控制 ---------- */

/**
 * @brief 使能输出 (ENABLE = L)
 */
void a4988_enable(void);

/**
 * @brief 禁用输出 (ENABLE = H)，FET 高阻态
 */
void a4988_disable(void);

/**
 * @brief 获取使能状态
 *
 * @return 0 = 输出使能中, 1 = 输出已禁用
 */
uint8_t a4988_is_enabled(void);

/* ---------- 复位控制 ---------- */

/**
 * @brief 复位译码器 (RESET = L)
 *
 * 所有 FET 输出关闭，译码器回到 Home 状态。
 * STEP 输入将被忽略，直到 RESET 释放。
 */
void a4988_reset_assert(void);

/**
 * @brief 释放复位 (RESET = H)，恢复正常操作
 */
void a4988_reset_release(void);

/* ---------- 睡眠控制 ---------- */

/**
 * @brief 进入睡眠模式 (SLEEP = L)
 *
 * 内部电路（FET、电流调节器、电荷泵）全部关闭以节省功耗。
 */
void a4988_sleep_enter(void);

/**
 * @brief 退出睡眠模式 (SLEEP = H)
 *
 * 芯片回到 Home 微步位置。从睡眠唤醒后需等待 ≥1ms 才能发送 STEP 命令。
 */
void a4988_sleep_exit(void);

/* ---------- 电流计算 ---------- */

/**
 * @brief 计算 ITripMAX (mA)
 *
 * ITripMAX = VREF / (8 × RS)
 *
 * @param vref_mv  VREF 电压 (mV)
 * @param rs_mohm  检流电阻 (mΩ)
 * @return ITripMAX (mA)
 */
uint32_t a4988_calc_itrip_max(uint16_t vref_mv, uint16_t rs_mohm);

/**
 * @brief 计算给定微步位置的相电流
 *
 * @param step_index   微步索引 (0-63，对应 Table 2)
 * @param itrip_max_mA ITripMAX (mA)
 * @param phase1_mA    输出参数 — Phase 1 电流 (mA)，可为 NULL
 * @param phase2_mA    输出参数 — Phase 2 电流 (mA)，可为 NULL
 */
void a4988_calc_phase_currents(uint8_t  step_index,
                                uint32_t itrip_max_mA,
                                int32_t  *phase1_mA,
                                int32_t  *phase2_mA);

/* ---------- 状态查询 ---------- */

/**
 * @brief 获取当前运行状态
 */
A4988_State_t a4988_get_state(void);

/* ---------- 工具函数 ---------- */

/**
 * @brief 将转速 (RPM) 转换为步进间隔 (µs)
 *
 * @param rpm        目标转速 (转/分钟)
 * @param ms         微步分辨率
 * @return 步进脉冲间隔 (µs)；返回 0 表示参数无效
 */
uint32_t a4988_rpm_to_delay_us(float rpm, A4988_Microstep_t ms);

/**
 * @brief 将步进间隔 (µs) 转换为转速 (RPM)
 *
 * @param delay_us   步进间隔 (µs)
 * @param ms         微步分辨率
 * @return 转速 (RPM)
 */
float a4988_delay_us_to_rpm(uint32_t delay_us, A4988_Microstep_t ms);

#endif /* A4988_H */
