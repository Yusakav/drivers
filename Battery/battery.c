/**
 * @file battery.c
 * @brief Generic battery profile table and lookup API.
 */

#include "battery.h"

#define BATTERY_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define PROFILE(_chem, _cells, _name, _vcell, _minsys_cell, _pre_cell)       \
    {                                                                       \
        .chemistry = (_chem),                                               \
        .cells = (_cells),                                                  \
        .chemistry_name = (_name),                                          \
        .charge_voltage_per_cell_mv = (_vcell),                             \
        .charge_voltage_mv = (uint16_t)((_vcell) * (uint16_t)(_cells)),     \
        .min_system_voltage_mv =                                            \
            (uint16_t)((_minsys_cell) * (uint16_t)(_cells)),                \
        .precharge_threshold_mv =                                           \
            (uint16_t)((_pre_cell) * (uint16_t)(_cells)),                   \
        .recommended_charge_current_ma =                                    \
            (uint16_t)(((_cells) == BATTERY_CELLS_1S) ? 1000U : 2000U),     \
        .termination_current_ma =                                           \
            (uint16_t)(((_cells) == BATTERY_CELLS_1S) ? 100U : 200U),       \
        .pack_ovp_mv =                                                      \
            (uint16_t)((uint16_t)((_vcell) * (uint16_t)(_cells)) + 1000U),  \
    }

static const battery_profile_t s_profiles[] = {
    PROFILE(BATTERY_CHEM_LIFEPO4, BATTERY_CELLS_1S, "LiFePO4", 3600U, 3072U, 3000U),
    PROFILE(BATTERY_CHEM_LIFEPO4, BATTERY_CELLS_2S, "LiFePO4", 3600U, 3072U, 3000U),
    PROFILE(BATTERY_CHEM_LIFEPO4, BATTERY_CELLS_3S, "LiFePO4", 3600U, 3072U, 3000U),
    PROFILE(BATTERY_CHEM_LIFEPO4, BATTERY_CELLS_4S, "LiFePO4", 3600U, 3072U, 3000U),

    PROFILE(BATTERY_CHEM_NMC, BATTERY_CELLS_1S, "NMC", 4200U, 3584U, 3000U),
    PROFILE(BATTERY_CHEM_NMC, BATTERY_CELLS_2S, "NMC", 4200U, 3584U, 3000U),
    PROFILE(BATTERY_CHEM_NMC, BATTERY_CELLS_3S, "NMC", 4200U, 3584U, 3000U),
    PROFILE(BATTERY_CHEM_NMC, BATTERY_CELLS_4S, "NMC", 4200U, 3584U, 3000U),

    PROFILE(BATTERY_CHEM_LIPO, BATTERY_CELLS_1S, "LiPo", 4200U, 3584U, 3000U),
    PROFILE(BATTERY_CHEM_LIPO, BATTERY_CELLS_2S, "LiPo", 4200U, 3584U, 3000U),
    PROFILE(BATTERY_CHEM_LIPO, BATTERY_CELLS_3S, "LiPo", 4200U, 3584U, 3000U),
    PROFILE(BATTERY_CHEM_LIPO, BATTERY_CELLS_4S, "LiPo", 4200U, 3584U, 3000U),

    PROFILE(BATTERY_CHEM_LCO, BATTERY_CELLS_1S, "LCO", 4200U, 3584U, 3000U),
    PROFILE(BATTERY_CHEM_LCO, BATTERY_CELLS_2S, "LCO", 4200U, 3584U, 3000U),
    PROFILE(BATTERY_CHEM_LCO, BATTERY_CELLS_3S, "LCO", 4200U, 3584U, 3000U),
    PROFILE(BATTERY_CHEM_LCO, BATTERY_CELLS_4S, "LCO", 4200U, 3584U, 3000U),

    PROFILE(BATTERY_CHEM_LTO, BATTERY_CELLS_1S, "LTO", 2800U, 2048U, 1800U),
    PROFILE(BATTERY_CHEM_LTO, BATTERY_CELLS_2S, "LTO", 2800U, 2048U, 1800U),
    PROFILE(BATTERY_CHEM_LTO, BATTERY_CELLS_3S, "LTO", 2800U, 2048U, 1800U),
    PROFILE(BATTERY_CHEM_LTO, BATTERY_CELLS_4S, "LTO", 2800U, 2048U, 1800U),
};

const battery_profile_t *battery_get_profile(battery_chemistry_t chemistry,
                                             battery_cell_count_t cells)
{
    size_t i;

    if ((chemistry == BATTERY_CHEM_INVALID) ||
        (cells == BATTERY_CELLS_INVALID)) {
        return NULL;
    }

    for (i = 0U; i < BATTERY_ARRAY_SIZE(s_profiles); ++i) {
        if ((s_profiles[i].chemistry == chemistry) &&
            (s_profiles[i].cells == cells)) {
            return &s_profiles[i];
        }
    }

    return NULL;
}

battery_ret_t battery_copy_profile(battery_chemistry_t chemistry,
                                   battery_cell_count_t cells,
                                   battery_profile_t *profile_out)
{
    const battery_profile_t *profile;

    if (profile_out == NULL) {
        return BATTERY_RET_NULL;
    }

    profile = battery_get_profile(chemistry, cells);
    if (profile == NULL) {
        return BATTERY_RET_NOT_FOUND;
    }

    *profile_out = *profile;
    return BATTERY_RET_OK;
}

size_t battery_profile_count(void)
{
    return BATTERY_ARRAY_SIZE(s_profiles);
}

const char *battery_chemistry_name(battery_chemistry_t chemistry)
{
    switch (chemistry) {
    case BATTERY_CHEM_LIFEPO4:
        return "LiFePO4";
    case BATTERY_CHEM_NMC:
        return "NMC";
    case BATTERY_CHEM_LIPO:
        return "LiPo";
    case BATTERY_CHEM_LCO:
        return "LCO";
    case BATTERY_CHEM_LTO:
        return "LTO";
    default:
        return "Invalid";
    }
}

uint8_t battery_cells_value(battery_cell_count_t cells)
{
    if ((cells < BATTERY_CELLS_1S) || (cells > BATTERY_CELLS_4S)) {
        return 0U;
    }

    return (uint8_t)cells;
}
