/**
 * @file battery_ti.c
 * @brief Texas Instruments charger parameter adapters for battery profiles.
 */

#include "battery_ti.h"

#define BQ25710_CHG_CURR_MIN_MA       0U
#define BQ25710_CHG_CURR_MAX_MA       8128U
#define BQ25710_CHG_CURR_STEP_MA      64U
#define BQ25710_CHG_VOLT_MIN_MV       0U
#define BQ25710_CHG_VOLT_MAX_MV       22392U
#define BQ25710_CHG_VOLT_STEP_MV      8U
#define BQ25710_MINSYS_MIN_MV         0U
#define BQ25710_MINSYS_MAX_MV         16128U
#define BQ25710_MINSYS_STEP_MV        256U

#define BQ25895_SYS_MIN_MIN_MV        3000U
#define BQ25895_SYS_MIN_MAX_MV        3700U
#define BQ25895_SYS_MIN_STEP_MV       100U
#define BQ25895_ICHG_MIN_MA           0U
#define BQ25895_ICHG_MAX_MA           5056U
#define BQ25895_ICHG_STEP_MA          64U
#define BQ25895_IPRECHG_MIN_MA        64U
#define BQ25895_IPRECHG_MAX_MA        1024U
#define BQ25895_IPRECHG_STEP_MA       64U
#define BQ25895_ITERM_MIN_MA          64U
#define BQ25895_ITERM_MAX_MA          1024U
#define BQ25895_ITERM_STEP_MA         64U
#define BQ25895_VREG_MIN_MV           3840U
#define BQ25895_VREG_MAX_MV           4608U
#define BQ25895_VREG_STEP_MV          16U

static uint16_t align_down_u16(uint16_t value, uint16_t step)
{
    if (step == 0U) {
        return value;
    }

    return (uint16_t)(value - (uint16_t)(value % step));
}

static uint16_t align_up_u16(uint16_t value, uint16_t step)
{
    uint16_t rem;

    if (step == 0U) {
        return value;
    }

    rem = (uint16_t)(value % step);
    if (rem == 0U) {
        return value;
    }

    return (uint16_t)(value + (uint16_t)(step - rem));
}

static uint16_t clamp_u16(uint16_t value, uint16_t min, uint16_t max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static battery_ret_t range_check_u16(uint16_t value, uint16_t min, uint16_t max)
{
    if ((value < min) || (value > max)) {
        return BATTERY_RET_RANGE;
    }
    return BATTERY_RET_OK;
}

battery_ret_t battery_ti_get_bq25710_params(battery_chemistry_t chemistry,
                                            battery_cell_count_t cells,
                                            battery_ti_bq25710_params_t *params_out)
{
    const battery_profile_t *profile;
    uint16_t chg_voltage_mv;
    uint16_t min_sys_voltage_mv;
    uint16_t charge_current_ma;

    if (params_out == NULL) {
        return BATTERY_RET_NULL;
    }

    profile = battery_get_profile(chemistry, cells);
    if (profile == NULL) {
        return BATTERY_RET_NOT_FOUND;
    }

    chg_voltage_mv = align_down_u16(profile->charge_voltage_mv,
                                    BQ25710_CHG_VOLT_STEP_MV);
    min_sys_voltage_mv = align_up_u16(profile->min_system_voltage_mv,
                                      BQ25710_MINSYS_STEP_MV);
    charge_current_ma = align_down_u16(profile->recommended_charge_current_ma,
                                       BQ25710_CHG_CURR_STEP_MA);

    if (range_check_u16(chg_voltage_mv, BQ25710_CHG_VOLT_MIN_MV,
                        BQ25710_CHG_VOLT_MAX_MV) != BATTERY_RET_OK) {
        return BATTERY_RET_RANGE;
    }
    if (range_check_u16(min_sys_voltage_mv, BQ25710_MINSYS_MIN_MV,
                        BQ25710_MINSYS_MAX_MV) != BATTERY_RET_OK) {
        return BATTERY_RET_RANGE;
    }
    if (range_check_u16(charge_current_ma, BQ25710_CHG_CURR_MIN_MA,
                        BQ25710_CHG_CURR_MAX_MA) != BATTERY_RET_OK) {
        return BATTERY_RET_RANGE;
    }

    params_out->chemistry = profile->chemistry;
    params_out->cells = profile->cells;
    params_out->chemistry_name = profile->chemistry_name;
    params_out->chg_voltage_mv = chg_voltage_mv;
    params_out->chg_voltage_per_cell_mv = profile->charge_voltage_per_cell_mv;
    params_out->min_sys_voltage_mv = min_sys_voltage_mv;
    params_out->precharge_thresh_mv = profile->precharge_threshold_mv;
    params_out->charge_current_ma = charge_current_ma;
    params_out->term_current_ma = profile->termination_current_ma;
    params_out->sysovp_thresh_mv = profile->pack_ovp_mv;

    return BATTERY_RET_OK;
}

battery_ret_t battery_ti_get_bq25895_params(battery_chemistry_t chemistry,
                                            battery_cell_count_t cells,
                                            battery_ti_bq25895_params_t *params_out)
{
    const battery_profile_t *profile;
    uint16_t charge_voltage_mv;
    uint16_t charge_current_ma;
    uint16_t precharge_current_ma;
    uint16_t termination_current_ma;
    uint16_t sys_min_mv;

    if (params_out == NULL) {
        return BATTERY_RET_NULL;
    }

    if (cells != BATTERY_CELLS_1S) {
        return BATTERY_RET_UNSUPPORTED;
    }

    if ((chemistry != BATTERY_CHEM_NMC) &&
        (chemistry != BATTERY_CHEM_LIPO) &&
        (chemistry != BATTERY_CHEM_LCO)) {
        return BATTERY_RET_UNSUPPORTED;
    }

    profile = battery_get_profile(chemistry, cells);
    if (profile == NULL) {
        return BATTERY_RET_NOT_FOUND;
    }

    if ((profile->charge_voltage_mv < BQ25895_VREG_MIN_MV) ||
        (profile->charge_voltage_mv > BQ25895_VREG_MAX_MV)) {
        return BATTERY_RET_RANGE;
    }

    charge_voltage_mv = align_down_u16(profile->charge_voltage_mv,
                                       BQ25895_VREG_STEP_MV);
    charge_current_ma = align_down_u16(profile->recommended_charge_current_ma,
                                       BQ25895_ICHG_STEP_MA);
    precharge_current_ma = align_down_u16(profile->termination_current_ma,
                                          BQ25895_IPRECHG_STEP_MA);
    termination_current_ma = align_down_u16(profile->termination_current_ma,
                                            BQ25895_ITERM_STEP_MA);
    sys_min_mv = align_up_u16(profile->min_system_voltage_mv,
                              BQ25895_SYS_MIN_STEP_MV);

    charge_current_ma = clamp_u16(charge_current_ma, BQ25895_ICHG_MIN_MA,
                                  BQ25895_ICHG_MAX_MA);
    precharge_current_ma = clamp_u16(precharge_current_ma, BQ25895_IPRECHG_MIN_MA,
                                     BQ25895_IPRECHG_MAX_MA);
    termination_current_ma = clamp_u16(termination_current_ma,
                                       BQ25895_ITERM_MIN_MA,
                                       BQ25895_ITERM_MAX_MA);
    sys_min_mv = clamp_u16(sys_min_mv, BQ25895_SYS_MIN_MIN_MV,
                           BQ25895_SYS_MIN_MAX_MV);

    params_out->chemistry = profile->chemistry;
    params_out->cells = profile->cells;
    params_out->chemistry_name = profile->chemistry_name;
    params_out->charge_voltage_mv = charge_voltage_mv;
    params_out->charge_current_ma = charge_current_ma;
    params_out->precharge_current_ma = precharge_current_ma;
    params_out->termination_current_ma = termination_current_ma;
    params_out->sys_min_mv = sys_min_mv;

    return BATTERY_RET_OK;
}
