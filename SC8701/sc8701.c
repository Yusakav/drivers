/**
 * @file sc8701.c
 * @brief Generic SC8701 synchronous buck-boost controller driver.
 */

#include "sc8701.h"

#include <string.h>

static uint8_t valid_pwm_freq(uint32_t freq_hz)
{
    return (uint8_t)((freq_hz >= SC8701_PWM_FREQ_MIN_HZ) &&
                     (freq_hz <= SC8701_PWM_FREQ_MAX_HZ));
}

static int8_t validate_hal(const sc8701_hal_t *hal)
{
    if (hal == NULL) {
        return SC8701_ERR_NULL;
    }
    if ((hal->ce_set == NULL) || (hal->itune_set == NULL) ||
        (hal->pg_get == NULL) ||
        (hal->pwm_vout_init == NULL) || (hal->pwm_vout_set == NULL) ||
        (hal->pwm_ilim_init == NULL) || (hal->pwm_ilim_set == NULL) ||
        (hal->adc_read_mv == NULL) || (hal->delay_ms == NULL) ||
        (hal->get_ms == NULL)) {
        return SC8701_ERR_HW;
    }
    return SC8701_OK;
}

static int8_t validate_hw(const sc8701_hw_config_t *hw)
{
    if (hw == NULL) {
        return SC8701_ERR_NULL;
    }
    if ((hw->r_up_ohm == 0U) || (hw->r_down_ohm == 0U) ||
        (hw->rilim1_ohm == 0U) || (hw->rilim2_ohm == 0U) ||
        (hw->rsns1_uohm == 0U) || (hw->rsns2_uohm == 0U)) {
        return SC8701_ERR_PARAM;
    }
    if (!valid_pwm_freq(hw->pwm_vout_freq_hz) ||
        !valid_pwm_freq(hw->pwm_ilim_freq_hz)) {
        return SC8701_ERR_RANGE;
    }
    return SC8701_OK;
}

static uint32_t calc_vout_set_mv(const sc8701_hw_config_t *hw)
{
    uint64_t vout_mv = (uint64_t)SC8701_VFB_REF_MV *
                       ((uint64_t)hw->r_up_ohm + (uint64_t)hw->r_down_ohm);
    vout_mv /= (uint64_t)hw->r_down_ohm;
    return (uint32_t)vout_mv;
}

static uint32_t calc_ilim_ma(uint32_t rilim_ohm, uint32_t rsns_uohm)
{
    uint64_t numerator = (uint64_t)SC8701_ILIM_REF_MV *
                         (uint64_t)SC8701_ILIM_GAIN_NUM *
                         1000000ULL;
    uint64_t denominator = (uint64_t)rilim_ohm * (uint64_t)rsns_uohm;

    return (uint32_t)(numerator / denominator);
}

static float vout_to_duty(uint32_t target_mv, uint32_t vout_set_mv)
{
    float ratio;
    float duty;

    if (vout_set_mv == 0U) {
        return 0.0f;
    }

    ratio = (float)target_mv / (float)vout_set_mv;
    duty = ((6.0f * ratio) - 1.0f) / 5.0f;
    if (duty < 0.0f) {
        duty = 0.0f;
    }
    if (duty > 1.0f) {
        duty = 1.0f;
    }
    return duty;
}

static float ilim_to_duty(uint32_t target_ma, uint32_t limit_ma)
{
    float duty;

    if (limit_ma == 0U) {
        return 0.0f;
    }

    duty = (float)target_ma / (float)limit_ma;
    if (duty < 0.0f) {
        duty = 0.0f;
    }
    if (duty > 1.0f) {
        duty = 1.0f;
    }
    return duty;
}

int8_t sc8701_init(sc8701_ctx_t *ctx,
                   const sc8701_hal_t *hal,
                   void *user,
                   const sc8701_hw_config_t *hw)
{
    int8_t ret;

    if (ctx == NULL) {
        return SC8701_ERR_NULL;
    }

    ret = validate_hal(hal);
    if (ret != SC8701_OK) {
        return ret;
    }
    ret = validate_hw(hw);
    if (ret != SC8701_OK) {
        return ret;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->hal = hal;
    ctx->user = user;
    ctx->hw = *hw;

    ctx->vout_set_mv = calc_vout_set_mv(hw);
    if ((ctx->vout_set_mv < SC8701_VOUT_MIN_MV) ||
        (ctx->vout_set_mv > SC8701_VOUT_MAX_MV)) {
        memset(ctx, 0, sizeof(*ctx));
        return SC8701_ERR_RANGE;
    }

    ctx->iin_lim_ma = calc_ilim_ma(hw->rilim1_ohm, hw->rsns1_uohm);
    ctx->iout_lim_ma = calc_ilim_ma(hw->rilim2_ohm, hw->rsns2_uohm);
    if ((ctx->iin_lim_ma == 0U) || (ctx->iout_lim_ma == 0U)) {
        memset(ctx, 0, sizeof(*ctx));
        return SC8701_ERR_RANGE;
    }

    ret = hal->pwm_vout_init(user, hw->pwm_vout_freq_hz);
    if (ret != 0) {
        memset(ctx, 0, sizeof(*ctx));
        return SC8701_ERR_HW;
    }
    ret = hal->pwm_ilim_init(user, hw->pwm_ilim_freq_hz);
    if (ret != 0) {
        memset(ctx, 0, sizeof(*ctx));
        return SC8701_ERR_HW;
    }

    ctx->vout_target_mv = ctx->vout_set_mv;
    ctx->ilim_target_ma = ctx->iin_lim_ma;
    ctx->itune_target = SC8701_ITUNE_INPUT;

    hal->ce_set(user, 1U);
    hal->pwm_vout_set(user, 1.0f);
    hal->pwm_ilim_set(user, 1.0f);
    hal->itune_set(user, (uint8_t)SC8701_ITUNE_INPUT);

    return SC8701_OK;
}

int8_t sc8701_enable(sc8701_ctx_t *ctx, uint32_t timeout_ms)
{
    const sc8701_hal_t *hal;
    uint32_t start_ms;

    if (ctx == NULL) {
        return SC8701_ERR_NULL;
    }
    if ((ctx->hal == NULL) || (ctx->hal->itune_set == NULL)) {
        return SC8701_ERR_PARAM;
    }
    if (ctx->fault != 0U) {
        return SC8701_ERR_FAULT;
    }
    if (ctx->enabled != 0U) {
        return SC8701_OK;
    }

    hal = ctx->hal;
    hal->ce_set(ctx->user, 0U);
    ctx->enabled = 1U;
    ctx->pg_ok = 0U;
    ctx->enable_time_ms = hal->get_ms(ctx->user);
    hal->delay_ms(ctx->user, SC8701_SOFT_START_MS_MAX);

    if (timeout_ms == 0U) {
        return SC8701_OK;
    }

    start_ms = hal->get_ms(ctx->user);
    while ((uint32_t)(hal->get_ms(ctx->user) - start_ms) < timeout_ms) {
        ctx->pg_ok = hal->pg_get(ctx->user);
        if (ctx->pg_ok != 0U) {
            return SC8701_OK;
        }
        hal->delay_ms(ctx->user, 1U);
    }

    ctx->fault = 1U;
    ctx->enabled = 0U;
    ctx->pg_ok = 0U;
    hal->ce_set(ctx->user, 1U);
    return SC8701_ERR_PG_TIMEOUT;
}

int8_t sc8701_disable(sc8701_ctx_t *ctx)
{
    if (ctx == NULL) {
        return SC8701_ERR_NULL;
    }
    if (ctx->hal == NULL) {
        return SC8701_ERR_PARAM;
    }

    ctx->hal->ce_set(ctx->user, 1U);
    ctx->enabled = 0U;
    ctx->pg_ok = 0U;
    return SC8701_OK;
}

int8_t sc8701_set_vout(sc8701_ctx_t *ctx, uint32_t target_mv)
{
    uint32_t min_mv;
    float duty;

    if (ctx == NULL) {
        return SC8701_ERR_NULL;
    }
    if ((ctx->hal == NULL) || (ctx->hal->pwm_vout_set == NULL)) {
        return SC8701_ERR_PARAM;
    }

    min_mv = ctx->vout_set_mv / 6U;
    if (min_mv < SC8701_VOUT_MIN_MV) {
        min_mv = SC8701_VOUT_MIN_MV;
    }
    if ((target_mv < min_mv) || (target_mv > ctx->vout_set_mv) ||
        (target_mv > SC8701_VOUT_MAX_MV)) {
        return SC8701_ERR_RANGE;
    }

    duty = vout_to_duty(target_mv, ctx->vout_set_mv);
    ctx->hal->pwm_vout_set(ctx->user, duty);
    ctx->vout_target_mv = target_mv;
    return SC8701_OK;
}

int8_t sc8701_set_ilim(sc8701_ctx_t *ctx, uint32_t target_ma)
{
    uint32_t limit_ma;
    float duty;

    if (ctx == NULL) {
        return SC8701_ERR_NULL;
    }
    if ((ctx->hal == NULL) || (ctx->hal->pwm_ilim_set == NULL)) {
        return SC8701_ERR_PARAM;
    }

    limit_ma = (ctx->itune_target == SC8701_ITUNE_INPUT) ?
               ctx->iin_lim_ma : ctx->iout_lim_ma;
    if ((target_ma == 0U) || (target_ma > limit_ma)) {
        return SC8701_ERR_RANGE;
    }

    duty = ilim_to_duty(target_ma, limit_ma);
    ctx->hal->pwm_ilim_set(ctx->user, duty);
    ctx->ilim_target_ma = target_ma;
    return SC8701_OK;
}

int8_t sc8701_set_itune_target(sc8701_ctx_t *ctx, sc8701_itune_target_t target)
{
    if (ctx == NULL) {
        return SC8701_ERR_NULL;
    }
    if (ctx->hal == NULL) {
        return SC8701_ERR_PARAM;
    }
    if ((target != SC8701_ITUNE_INPUT) && (target != SC8701_ITUNE_OUTPUT)) {
        return SC8701_ERR_PARAM;
    }

    ctx->itune_target = target;
    ctx->hal->itune_set(ctx->user, (uint8_t)target);
    return SC8701_OK;
}

int8_t sc8701_clear_fault(sc8701_ctx_t *ctx)
{
    if (ctx == NULL) {
        return SC8701_ERR_NULL;
    }
    ctx->fault = 0U;
    return SC8701_OK;
}

uint8_t sc8701_get_pg(sc8701_ctx_t *ctx)
{
    if ((ctx == NULL) || (ctx->hal == NULL) || (ctx->hal->pg_get == NULL)) {
        return 0U;
    }
    ctx->pg_ok = ctx->hal->pg_get(ctx->user);
    return ctx->pg_ok;
}

sc8701_mode_t sc8701_get_mode(sc8701_ctx_t *ctx)
{
    uint16_t vin_mv = 0U;
    uint16_t vout_mv = 0U;

    if ((ctx == NULL) || (ctx->hal == NULL)) {
        return SC8701_MODE_FAULT;
    }
    if (ctx->enabled == 0U) {
        return SC8701_MODE_OFF;
    }
    if (ctx->fault != 0U) {
        return SC8701_MODE_FAULT;
    }
    if (ctx->hal->adc_read_mv == NULL) {
        return SC8701_MODE_FAULT;
    }
    if ((ctx->hal->adc_read_mv(ctx->user, SC8701_ADC_VIN, &vin_mv) != 0) ||
        (ctx->hal->adc_read_mv(ctx->user, SC8701_ADC_VOUT, &vout_mv) != 0)) {
        return SC8701_MODE_BUCK_BOOST;
    }
    if (vin_mv > (uint16_t)(vout_mv + 500U)) {
        return SC8701_MODE_BUCK;
    }
    if (vout_mv > (uint16_t)(vin_mv + 500U)) {
        return SC8701_MODE_BOOST;
    }
    return SC8701_MODE_BUCK_BOOST;
}

uint8_t sc8701_is_enabled(const sc8701_ctx_t *ctx)
{
    return (ctx == NULL) ? 0U : ctx->enabled;
}

uint8_t sc8701_get_fault(const sc8701_ctx_t *ctx)
{
    return (ctx == NULL) ? 0U : ctx->fault;
}

uint32_t sc8701_get_vout_set_mv(const sc8701_ctx_t *ctx)
{
    return (ctx == NULL) ? 0U : ctx->vout_set_mv;
}

uint32_t sc8701_get_vout_target_mv(const sc8701_ctx_t *ctx)
{
    return (ctx == NULL) ? 0U : ctx->vout_target_mv;
}

uint32_t sc8701_get_iin_limit_ma(const sc8701_ctx_t *ctx)
{
    return (ctx == NULL) ? 0U : ctx->iin_lim_ma;
}

uint32_t sc8701_get_iout_limit_ma(const sc8701_ctx_t *ctx)
{
    return (ctx == NULL) ? 0U : ctx->iout_lim_ma;
}
