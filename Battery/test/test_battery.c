#include "battery.h"
#include "battery_ti.h"

#include <stdio.h>
#include <stdint.h>

#define TEST_ASSERT(cond)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("ASSERT failed at %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static int test_profiles(void)
{
    static const battery_chemistry_t chemistries[] = {
        BATTERY_CHEM_LIFEPO4,
        BATTERY_CHEM_NMC,
        BATTERY_CHEM_LIPO,
        BATTERY_CHEM_LCO,
        BATTERY_CHEM_LTO,
    };
    static const battery_cell_count_t cells[] = {
        BATTERY_CELLS_1S,
        BATTERY_CELLS_2S,
        BATTERY_CELLS_3S,
        BATTERY_CELLS_4S,
    };
    size_t i;
    size_t j;

    TEST_ASSERT(battery_profile_count() == 20U);
    TEST_ASSERT(battery_get_profile(BATTERY_CHEM_INVALID, BATTERY_CELLS_1S) == NULL);
    TEST_ASSERT(battery_get_profile(BATTERY_CHEM_NMC, BATTERY_CELLS_INVALID) == NULL);
    TEST_ASSERT(battery_copy_profile(BATTERY_CHEM_NMC, BATTERY_CELLS_1S, NULL) ==
                BATTERY_RET_NULL);

    for (i = 0U; i < (sizeof(chemistries) / sizeof(chemistries[0])); ++i) {
        for (j = 0U; j < (sizeof(cells) / sizeof(cells[0])); ++j) {
            battery_profile_t profile;
            TEST_ASSERT(battery_copy_profile(chemistries[i], cells[j], &profile) ==
                        BATTERY_RET_OK);
            TEST_ASSERT(profile.chemistry == chemistries[i]);
            TEST_ASSERT(profile.cells == cells[j]);
            TEST_ASSERT(profile.chemistry_name != NULL);
            TEST_ASSERT(profile.charge_voltage_mv ==
                        (uint16_t)(profile.charge_voltage_per_cell_mv *
                                   (uint16_t)profile.cells));
            TEST_ASSERT(profile.pack_ovp_mv > profile.charge_voltage_mv);
            TEST_ASSERT(battery_cells_value(cells[j]) == (uint8_t)cells[j]);
        }
    }

    TEST_ASSERT(battery_cells_value(BATTERY_CELLS_INVALID) == 0U);
    TEST_ASSERT(battery_chemistry_name(BATTERY_CHEM_LIFEPO4) != NULL);

    return 0;
}

static int test_bq25710_adapter(void)
{
    static const battery_chemistry_t chemistries[] = {
        BATTERY_CHEM_LIFEPO4,
        BATTERY_CHEM_NMC,
        BATTERY_CHEM_LIPO,
        BATTERY_CHEM_LCO,
        BATTERY_CHEM_LTO,
    };
    static const battery_cell_count_t cells[] = {
        BATTERY_CELLS_1S,
        BATTERY_CELLS_2S,
        BATTERY_CELLS_3S,
        BATTERY_CELLS_4S,
    };
    size_t i;
    size_t j;
    battery_ti_bq25710_params_t params;

    TEST_ASSERT(battery_ti_get_bq25710_params(BATTERY_CHEM_NMC, BATTERY_CELLS_1S,
                                              NULL) == BATTERY_RET_NULL);

    for (i = 0U; i < (sizeof(chemistries) / sizeof(chemistries[0])); ++i) {
        for (j = 0U; j < (sizeof(cells) / sizeof(cells[0])); ++j) {
            TEST_ASSERT(battery_ti_get_bq25710_params(chemistries[i], cells[j],
                                                      &params) == BATTERY_RET_OK);
            TEST_ASSERT((params.chg_voltage_mv % 8U) == 0U);
            TEST_ASSERT((params.min_sys_voltage_mv % 256U) == 0U);
            TEST_ASSERT((params.charge_current_ma % 64U) == 0U);
            TEST_ASSERT(params.sysovp_thresh_mv > params.chg_voltage_mv);
        }
    }

    TEST_ASSERT(battery_ti_get_bq25710_params(BATTERY_CHEM_NMC, BATTERY_CELLS_1S,
                                              &params) == BATTERY_RET_OK);
    TEST_ASSERT(params.charge_current_ma == 960U);

    TEST_ASSERT(battery_ti_get_bq25710_params(BATTERY_CHEM_NMC, BATTERY_CELLS_2S,
                                              &params) == BATTERY_RET_OK);
    TEST_ASSERT(params.charge_current_ma == 1984U);

    return 0;
}

static int test_bq25895_adapter(void)
{
    battery_ti_bq25895_params_t params;

    TEST_ASSERT(battery_ti_get_bq25895_params(BATTERY_CHEM_NMC, BATTERY_CELLS_1S,
                                              NULL) == BATTERY_RET_NULL);
    TEST_ASSERT(battery_ti_get_bq25895_params(BATTERY_CHEM_NMC, BATTERY_CELLS_1S,
                                              &params) == BATTERY_RET_OK);
    TEST_ASSERT(params.charge_voltage_mv == 4192U);
    TEST_ASSERT((params.charge_voltage_mv % 16U) == 0U);
    TEST_ASSERT((params.charge_current_ma % 64U) == 0U);
    TEST_ASSERT((params.precharge_current_ma % 64U) == 0U);
    TEST_ASSERT((params.termination_current_ma % 64U) == 0U);
    TEST_ASSERT((params.sys_min_mv % 100U) == 0U);
    TEST_ASSERT(params.sys_min_mv >= 3000U);
    TEST_ASSERT(params.sys_min_mv <= 3700U);

    TEST_ASSERT(battery_ti_get_bq25895_params(BATTERY_CHEM_LIPO, BATTERY_CELLS_1S,
                                              &params) == BATTERY_RET_OK);
    TEST_ASSERT(battery_ti_get_bq25895_params(BATTERY_CHEM_LCO, BATTERY_CELLS_1S,
                                              &params) == BATTERY_RET_OK);

    TEST_ASSERT(battery_ti_get_bq25895_params(BATTERY_CHEM_LIFEPO4, BATTERY_CELLS_1S,
                                              &params) == BATTERY_RET_UNSUPPORTED);
    TEST_ASSERT(battery_ti_get_bq25895_params(BATTERY_CHEM_LTO, BATTERY_CELLS_1S,
                                              &params) == BATTERY_RET_UNSUPPORTED);
    TEST_ASSERT(battery_ti_get_bq25895_params(BATTERY_CHEM_NMC, BATTERY_CELLS_2S,
                                              &params) == BATTERY_RET_UNSUPPORTED);

    return 0;
}

int main(void)
{
    int ret;

    ret = test_profiles();
    if (ret != 0) {
        return ret;
    }

    ret = test_bq25710_adapter();
    if (ret != 0) {
        return ret;
    }

    ret = test_bq25895_adapter();
    if (ret != 0) {
        return ret;
    }

    printf("Battery tests passed\n");
    return 0;
}
