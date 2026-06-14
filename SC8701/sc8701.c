/**
 * @file sc8701.c
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief SC8701 同步 Buck-Boost 控制器驱动实现
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 */

#include "sc8701.h"
#include <math.h>

/* ==================== 内部辅助宏 ==================== */

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef MIN
#define MIN(a, b)  ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b)  ((a) > (b) ? (a) : (b))
#endif

#ifndef CLAMP
#define CLAMP(v, lo, hi)  (MAX((lo), MIN((v), (hi))))
#endif

/* ==================== 静态辅助函数 ==================== */

/**
 * @brief 根据 FB 分压电阻计算硬件设定输出电压
 *
 * VOUT_SET = VFB_REF × (1 + RUP / RDOWN)
 */
static uint32_t calc_vout_set (const sc8701_hw_config_t *hw)
{
    /* VOUT = 1220mV × (RUP + RDOWN) / RDOWN */
    uint64_t vout = (uint64_t)SC8701_VFB_REF_MV * ((uint64_t)hw->r_up_ohm + (uint64_t)hw->r_down_ohm)
                    / (uint64_t)hw->r_down_ohm;
    return (uint32_t)vout;
}

/**
 * @brief 根据 ILIM 电阻和检流电阻计算电流限制
 *
 * ILIM = VREF / RILIM × RSS / RSNS
 * 返回 mA
 */
static uint32_t calc_ilim (uint32_t rilim_ohm, uint32_t rss_ohm, uint32_t rsns_uohm)
{
    /* ILIM(A) = VREF / RILIM × RSS / RSNS
       VREF = 1.21V, RSS 通常等于 RSNS 的取值电阻 (数据手册未单独定义 RSS)
       根据手册: ILIM = VREF / RILIM × RSS / RSNS
       通常 RSS = RILIM 的内部分压电阻，实际使用中 ILIM1 和 ILIM2
       的引脚电阻直接设定限流: ILIM = 1.21V / RILIM (此处的 RILIM 已含比例换算)
       
       保守做法：假设 RSS/RSNS = 1 (即 RSS=RSNS)，则 ILIM = 1.21 / RILIM
       更精确需要根据实际 PCB 设计。此处按照典型公式:
       ILIM(mA) = 1210 / RILIM(Ω) × RSS/RSNS × 1000
       
       如果 RSS 未独立配置，默认 RSS = RSNS:
       ILIM(mA) = 1210 / RILIM(Ω) × 1000
    */
    uint64_t ilim_ua;
    
    if (rilim_ohm == 0 || rsns_uohm == 0) {
        return 0;
    }

    if (rss_ohm == 0) {
        /* RSS 未指定时默认等于 RSNS */
        /* ILIM(μA) = 1210000 / RILIM */
        ilim_ua = (uint64_t)SC8701_VREF_MV * 1000ULL / (uint64_t)rilim_ohm;
    } else {
        /* ILIM(μA) = 1210000 / RILIM × RSS / RSNS */
        ilim_ua = (uint64_t)SC8701_VREF_MV * 1000ULL * (uint64_t)rss_ohm
                  / ((uint64_t)rilim_ohm * (uint64_t)(rsns_uohm / 1000));
    }

    return (uint32_t)(ilim_ua / 1000ULL);
}

/**
 * @brief 根据目标输出电压计算 PWM 占空比
 *
 * VOUT = VOUT_SET × (1/6 + 5/6 × D)
 * D = (VOUT / VOUT_SET - 1/6) × 6/5
 *   = (6 × VOUT / VOUT_SET - 1) / 5
 */
static float vout_to_duty (uint32_t vout_target_mv, uint32_t vout_set_mv)
{
    if (vout_set_mv == 0) {
        return 0.0f;
    }
    float ratio = (float)vout_target_mv / (float)vout_set_mv;
    float duty  = (6.0f * ratio - 1.0f) / 5.0f;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    return duty;
}

/**
 * @brief 根据目标限流值计算 IPWM 占空比
 *
 * ILIM = ILIM_SET × D
 * D = ILIM / ILIM_SET
 */
static float ilim_to_duty (uint32_t ilim_target_ma, uint32_t ilim_set_ma)
{
    if (ilim_set_ma == 0) {
        return 0.0f;
    }
    float duty = (float)ilim_target_ma / (float)ilim_set_ma;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    return duty;
}

/* ==================== 公开 API 实现 ==================== */

/**
 * @brief 初始化 SC8701 驱动
 */
int8_t sc8701_init (sc8701_ctx_t *ctx,
                    const sc8701_hal_t *hal,
                    const sc8701_hw_config_t *hw)
{
    if (!ctx || !hal || !hw) {
        return SC8701_ERR_PARAM;
    }

    if (!hal->ce_set || !hal->pg_get || !hal->pwm_vout_init ||
        !hal->pwm_vout_set || !hal->delay_ms || !hal->get_ms) {
        return SC8701_ERR_HW;
    }

    /* 清零上下文 */
    ctx->hal       = hal;
    ctx->hw        = hw;
    ctx->enabled   = 0;
    ctx->pg_ok     = 0;
    ctx->fault     = 0;
    ctx->enable_time_ms = 0;

    /* 计算硬件设定参数 */
    ctx->vout_set_mv  = calc_vout_set(hw);
    ctx->iin_lim_ma   = calc_ilim(hw->rilim1_ohm, 0, hw->rsns1_uohm);
    ctx->iout_lim_ma  = calc_ilim(hw->rilim2_ohm, 0, hw->rsns2_uohm);

    /* 初始化动态参数为硬件设定值 */
    ctx->vout_target_mv  = ctx->vout_set_mv;
    ctx->ilim_target_ma  = ctx->iin_lim_ma;
    ctx->itune_target    = SC8701_ITUNE_INPUT;

    /* 默认 PWM 频率 */
    ctx->pwm_vout_freq_hz = 50000U;   /* 50kHz */
    ctx->pwm_ilim_freq_hz = 50000U;   /* 50kHz */

    /* 初始化 PWM 外设 */
    int8_t ret = hal->pwm_vout_init(ctx->pwm_vout_freq_hz);
    if (ret != 0) {
        return SC8701_ERR_HW;
    }

    if (hal->pwm_ilim_init) {
        ret = hal->pwm_ilim_init(ctx->pwm_ilim_freq_hz);
        if (ret != 0) {
            return SC8701_ERR_HW;
        }
    }

    /* 初始状态：/CE 拉高 (关断), PWM 占空比 100% (默认全输出) */
    hal->ce_set(1);
    hal->pwm_vout_set(1.0f);

    if (hal->pwm_ilim_set) {
        hal->pwm_ilim_set(1.0f);
    }

    /* ITUNE 默认控制输入电流 */
    if (hal->itune_set) {
        hal->itune_set((uint8_t)SC8701_ITUNE_INPUT);
    }

    return SC8701_OK;
}

/**
 * @brief 使能 SC8701
 */
int8_t sc8701_enable (sc8701_ctx_t *ctx, uint32_t timeout_ms)
{
    if (!ctx || !ctx->hal) {
        return SC8701_ERR_PARAM;
    }

    if (ctx->enabled) {
        return SC8701_OK;  /* 已经使能 */
    }

    const sc8701_hal_t *hal = ctx->hal;

    /* 拉低 /CE 使能芯片 */
    hal->ce_set(0);
    ctx->enabled        = 1;
    ctx->enable_time_ms = hal->get_ms();

    /* 等待软启动 */
    hal->delay_ms(SC8701_SOFT_START_MS_MAX);

    /* 如果指定了超时，等待 PG */
    if (timeout_ms > 0) {
        uint32_t start_ms = hal->get_ms();
        uint32_t elapsed;

        do {
            ctx->pg_ok = hal->pg_get();
            if (ctx->pg_ok) {
                return SC8701_OK;
            }
            hal->delay_ms(1);
            elapsed = hal->get_ms() - start_ms;
        } while (elapsed < timeout_ms);

        /* PG 超时 */
        ctx->fault = 1;
        return SC8701_ERR_PG_TIMEOUT;
    }

    return SC8701_OK;
}

/**
 * @brief 关断 SC8701
 */
int8_t sc8701_disable (sc8701_ctx_t *ctx)
{
    if (!ctx || !ctx->hal) {
        return SC8701_ERR_PARAM;
    }

    ctx->hal->ce_set(1);
    ctx->enabled = 0;
    ctx->pg_ok   = 0;

    return SC8701_OK;
}

/**
 * @brief 动态调节输出电压
 */
int8_t sc8701_set_vout (sc8701_ctx_t *ctx, uint32_t target_mv)
{
    if (!ctx || !ctx->hal) {
        return SC8701_ERR_PARAM;
    }

    /* 钳位到芯片允许范围 */
    target_mv = CLAMP(target_mv, SC8701_VOUT_MIN_MV, SC8701_VOUT_MAX_MV);

    ctx->vout_target_mv = target_mv;

    /* 计算 PWM 占空比并输出 */
    float duty = vout_to_duty(target_mv, ctx->vout_set_mv);
    ctx->hal->pwm_vout_set(duty);

    return SC8701_OK;
}

/**
 * @brief 动态调节电流限制
 */
int8_t sc8701_set_ilim (sc8701_ctx_t *ctx, uint32_t target_ma)
{
    if (!ctx || !ctx->hal) {
        return SC8701_ERR_PARAM;
    }

    if (!ctx->hal->pwm_ilim_set) {
        return SC8701_ERR_HW;  /* IPWM 未配置 */
    }

    ctx->ilim_target_ma = target_ma;

    /* 根据 ITUNE 目标选择基准限流值 */
    uint32_t ilim_set_ma = (ctx->itune_target == SC8701_ITUNE_INPUT)
                           ? ctx->iin_lim_ma
                           : ctx->iout_lim_ma;

    float duty = ilim_to_duty(target_ma, ilim_set_ma);
    ctx->hal->pwm_ilim_set(duty);

    return SC8701_OK;
}

/**
 * @brief 设置 IPWM 控制目标
 */
int8_t sc8701_set_itune_target (sc8701_ctx_t *ctx, sc8701_itune_target_t target)
{
    if (!ctx || !ctx->hal) {
        return SC8701_ERR_PARAM;
    }

    ctx->itune_target = target;

    if (ctx->hal->itune_set) {
        ctx->hal->itune_set((uint8_t)target);
    }

    return SC8701_OK;
}

/**
 * @brief 获取 Power Good 状态
 */
uint8_t sc8701_get_pg (sc8701_ctx_t *ctx)
{
    if (!ctx || !ctx->hal) {
        return 0;
    }

    ctx->pg_ok = ctx->hal->pg_get();
    return ctx->pg_ok;
}

/**
 * @brief 获取当前工作模式
 */
sc8701_mode_t sc8701_get_mode (sc8701_ctx_t *ctx)
{
    if (!ctx || !ctx->hal) {
        return SC8701_MODE_FAULT;
    }

    if (!ctx->enabled) {
        return SC8701_MODE_OFF;
    }

    if (ctx->fault) {
        return SC8701_MODE_FAULT;
    }

    /* 通过外部 ADC 读取 VIN/VOUT 来判断 Buck/Boost 模式 */
    if (ctx->hal->adc_read_mv) {
        uint16_t vin_mv  = 0;
        uint16_t vout_mv = 0;

        ctx->hal->adc_read_mv(SC8701_ADC_VIN,  &vin_mv);
        ctx->hal->adc_read_mv(SC8701_ADC_VOUT, &vout_mv);

        if (vin_mv > vout_mv + 500) {
            return SC8701_MODE_BUCK;
        } else if (vout_mv > vin_mv + 500) {
            return SC8701_MODE_BOOST;
        } else {
            return SC8701_MODE_BUCK_BOOST;
        }
    }

    /* 无 ADC 时默认返回 Buck-Boost (过渡模式) */
    return SC8701_MODE_BUCK_BOOST;
}
