/**
 * @file battery_ti.h
 * @brief Texas Instruments charger parameter adapters for battery profiles.
 */

#ifndef BATTERY_TI_H
#define BATTERY_TI_H

#include "battery.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    battery_chemistry_t chemistry;
    battery_cell_count_t cells;
    const char *chemistry_name;
    uint16_t chg_voltage_mv;
    uint16_t chg_voltage_per_cell_mv;
    uint16_t min_sys_voltage_mv;
    uint16_t precharge_thresh_mv;
    uint16_t charge_current_ma;
    uint16_t term_current_ma;
    uint16_t sysovp_thresh_mv;
} battery_ti_bq25710_params_t;

typedef struct {
    battery_chemistry_t chemistry;
    battery_cell_count_t cells;
    const char *chemistry_name;
    uint16_t charge_voltage_mv;
    uint16_t charge_current_ma;
    uint16_t precharge_current_ma;
    uint16_t termination_current_ma;
    uint16_t sys_min_mv;
} battery_ti_bq25895_params_t;

battery_ret_t battery_ti_get_bq25710_params(battery_chemistry_t chemistry,
                                            battery_cell_count_t cells,
                                            battery_ti_bq25710_params_t *params_out);
battery_ret_t battery_ti_get_bq25895_params(battery_chemistry_t chemistry,
                                            battery_cell_count_t cells,
                                            battery_ti_bq25895_params_t *params_out);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_TI_H */
