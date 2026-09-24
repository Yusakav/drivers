/**
 * @file battery.h
 * @brief Generic battery profile table and lookup API.
 */

#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BATTERY_RET_OK = 0,
    BATTERY_RET_NULL = -1,
    BATTERY_RET_NOT_FOUND = -2,
    BATTERY_RET_UNSUPPORTED = -3,
    BATTERY_RET_RANGE = -4,
} battery_ret_t;

typedef enum {
    BATTERY_CHEM_INVALID = 0,
    BATTERY_CHEM_LIFEPO4,
    BATTERY_CHEM_NMC,
    BATTERY_CHEM_LIPO,
    BATTERY_CHEM_LCO,
    BATTERY_CHEM_LTO,
} battery_chemistry_t;

typedef enum {
    BATTERY_CELLS_INVALID = 0,
    BATTERY_CELLS_1S = 1,
    BATTERY_CELLS_2S = 2,
    BATTERY_CELLS_3S = 3,
    BATTERY_CELLS_4S = 4,
} battery_cell_count_t;

typedef struct {
    battery_chemistry_t chemistry;
    battery_cell_count_t cells;
    const char *chemistry_name;
    uint16_t charge_voltage_per_cell_mv;
    uint16_t charge_voltage_mv;
    uint16_t min_system_voltage_mv;
    uint16_t precharge_threshold_mv;
    uint16_t recommended_charge_current_ma;
    uint16_t termination_current_ma;
    uint16_t pack_ovp_mv;
} battery_profile_t;

const battery_profile_t *battery_get_profile(battery_chemistry_t chemistry,
                                             battery_cell_count_t cells);
battery_ret_t battery_copy_profile(battery_chemistry_t chemistry,
                                   battery_cell_count_t cells,
                                   battery_profile_t *profile_out);
size_t battery_profile_count(void);
const char *battery_chemistry_name(battery_chemistry_t chemistry);
uint8_t battery_cells_value(battery_cell_count_t cells);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_H */
