/**
 * @file battery.c
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief 电池参数配置查找表
 * @version 0.1
 * @date 2026-06-13
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#include "battery.h"

/**
 * @brief 8种标准电池参数配置查找表 (按化学类型与节数顺序排列)
 * 
 */
const battery_params_t g_battery_params_table[BATTERY_CHEM_MAX][BATTERY_CELLS_MAX] = {
    
    /* ==================== 磷酸铁锂 (LiFePO4) 分组 ==================== */
    [BATTERY_CHEM_LIFEPO4] = 
    {
        /* 1S */
        [BATTERY_CELLS_1S] = 
        {
            .chemistry               = BATTERY_CHEM_LIFEPO4,                               /**< 化学类型: 磷酸铁锂 */ 
            .cells                   = BATTERY_CELLS_1S,                                   /**< 电池节数: 1S */
            .chg_voltage_mv          = BATTERY_PARAM_1x_LIFEPO4_CHG_VOLTAGE_MV,            /**< 总充电终止电压: 3.600V */
            .chg_voltage_per_cell_mv = BATTERY_PARAM_1x_LIFEPO4_CHG_VOLTAGE_PER_CELL_MV,   /**< 单节充电终止电压: 3.60V */
            .min_sys_voltage_mv      = BATTERY_PARAM_1x_LIFEPO4_MIN_SYS_VOLTAGE_MV,        /**< 最小系统电压: 3.072V */
            .precharge_thresh_mv     = BATTERY_PARAM_1x_LIFEPO4_PRECHARGE_THRESH_MV,       /**< 预充电阈值电压: 3.0V */
            .charge_current_ma       = BATTERY_PARAM_1x_LIFEPO4_CHARGE_CURRENT_MA,         /**< 典型快充充电电流: 1A */
            .term_current_ma         = BATTERY_PARAM_1x_LIFEPO4_TERM_CURRENT_MA,           /**< 充电截止电流: 100mA */
            .sysovp_thresh_mv        = BATTERY_PARAM_1x_LIFEPO4_SYSOVP_THRESH_MV           /**< 系统过压保护阈值: 4.6V */
        },
        /* 2S */
        [BATTERY_CELLS_2S] = 
        {
            .chemistry               = BATTERY_CHEM_LIFEPO4,                               /**< 化学类型: 磷酸铁锂 */
            .cells                   = BATTERY_CELLS_2S,                                   /**< 电池节数: 2S */
            .chg_voltage_mv          = BATTERY_PARAM_2x_LIFEPO4_CHG_VOLTAGE_MV,            /**< 总充电终止电压: 7.200V */
            .chg_voltage_per_cell_mv = BATTERY_PARAM_2x_LIFEPO4_CHG_VOLTAGE_PER_CELL_MV,   /**< 单节充电终止电压: 3.60V */
            .min_sys_voltage_mv      = BATTERY_PARAM_2x_LIFEPO4_MIN_SYS_VOLTAGE_MV,        /**< 最小系统电压: 6.144V */
            .precharge_thresh_mv     = BATTERY_PARAM_2x_LIFEPO4_PRECHARGE_THRESH_MV,       /**< 预充电阈值电压: 6.0V */
            .charge_current_ma       = BATTERY_PARAM_2x_LIFEPO4_CHARGE_CURRENT_MA,         /**< 典型快充充电电流: 2A */
            .term_current_ma         = BATTERY_PARAM_2x_LIFEPO4_TERM_CURRENT_MA,           /**< 充电截止电流: 200mA */
            .sysovp_thresh_mv        = BATTERY_PARAM_2x_LIFEPO4_SYSOVP_THRESH_MV           /**< 系统过压保护阈值: 8.2V */
        },
        /* 3S */
        [BATTERY_CELLS_3S] = 
        {
            .chemistry               = BATTERY_CHEM_LIFEPO4,                               /**< 化学类型: 磷酸铁锂 */
            .cells                   = BATTERY_CELLS_3S,                                   /**< 电池节数: 3S */
            .chg_voltage_mv          = BATTERY_PARAM_3x_LIFEPO4_CHG_VOLTAGE_MV,            /**< 总充电终止电压: 10.800V */
            .chg_voltage_per_cell_mv = BATTERY_PARAM_3x_LIFEPO4_CHG_VOLTAGE_PER_CELL_MV,   /**< 单节充电终止电压: 3.60V */
            .min_sys_voltage_mv      = BATTERY_PARAM_3x_LIFEPO4_MIN_SYS_VOLTAGE_MV,        /**< 最小系统电压: 9.216V */
            .precharge_thresh_mv     = BATTERY_PARAM_3x_LIFEPO4_PRECHARGE_THRESH_MV,       /**< 预充电阈值电压: 9.0V */
            .charge_current_ma       = BATTERY_PARAM_3x_LIFEPO4_CHARGE_CURRENT_MA,         /**< 典型快充充电电流: 2A */
            .term_current_ma         = BATTERY_PARAM_3x_LIFEPO4_TERM_CURRENT_MA,           /**< 充电截止电流: 200mA */
            .sysovp_thresh_mv        = BATTERY_PARAM_3x_LIFEPO4_SYSOVP_THRESH_MV           /**< 系统过压保护阈值: 11.8V */
        },
        /* 4S */
        [BATTERY_CELLS_4S] = 
        {
            .chemistry               = BATTERY_CHEM_LIFEPO4,                               /**< 化学类型: 磷酸铁锂 */
            .cells                   = BATTERY_CELLS_4S,                                   /**< 电池节数: 4S */
            .chg_voltage_mv          = BATTERY_PARAM_4x_LIFEPO4_CHG_VOLTAGE_MV,            /**< 总充电终止电压: 14.400V */
            .chg_voltage_per_cell_mv = BATTERY_PARAM_4x_LIFEPO4_CHG_VOLTAGE_PER_CELL_MV,   /**< 单节充电终止电压: 3.60V */
            .min_sys_voltage_mv      = BATTERY_PARAM_4x_LIFEPO4_MIN_SYS_VOLTAGE_MV,        /**< 最小系统电压: 12.288V */
            .precharge_thresh_mv     = BATTERY_PARAM_4x_LIFEPO4_PRECHARGE_THRESH_MV,       /**< 预充电阈值电压: 12.0V */
            .charge_current_ma       = BATTERY_PARAM_4x_LIFEPO4_CHARGE_CURRENT_MA,         /**< 典型快充充电电流: 2A */
            .term_current_ma         = BATTERY_PARAM_4x_LIFEPO4_TERM_CURRENT_MA,           /**< 充电截止电流: 200mA */
            .sysovp_thresh_mv        = BATTERY_PARAM_4x_LIFEPO4_SYSOVP_THRESH_MV           /**< 系统过压保护阈值: 15.4V */
        },
    },
    
    /* ==================== 三元锂 (NMC / Li-ion) 分组 ==================== */
    [BATTERY_CHEM_NMC] = 
    {
        [BATTERY_CELLS_1S] = 
        {
            .chemistry               = BATTERY_CHEM_NMC,                                   /**< 化学类型: 三元锂 */
            .cells                   = BATTERY_CELLS_1S,                                   /**< 电池节数: 1S */
            .chg_voltage_mv          = BATTERY_PARAM_1x_NMC_CHG_VOLTAGE_MV,                /**< 总充电终止电压: 4.200V */
            .chg_voltage_per_cell_mv = BATTERY_PARAM_1x_NMC_CHG_VOLTAGE_PER_CELL_MV,       /**< 单节充电终止电压: 4.20V */
            .min_sys_voltage_mv      = BATTERY_PARAM_1x_NMC_MIN_SYS_VOLTAGE_MV,            /**< 最小系统电压: 3.584V */
            .precharge_thresh_mv     = BATTERY_PARAM_1x_NMC_PRECHARGE_THRESH_MV,           /**< 预充电阈值电压: 3.0V */
            .charge_current_ma       = BATTERY_PARAM_1x_NMC_CHARGE_CURRENT_MA,             /**< 典型快充充电电流: 1A */
            .term_current_ma         = BATTERY_PARAM_1x_NMC_TERM_CURRENT_MA,               /**< 充电截止电流: 100mA */
            .sysovp_thresh_mv        = BATTERY_PARAM_1x_NMC_SYSOVP_THRESH_MV               /**< 系统过压保护阈值: 5.2V */
        },
        /* 2S */
        [BATTERY_CELLS_2S] = 
        {
            .chemistry               = BATTERY_CHEM_NMC,                                   /**< 化学类型: 三元锂 */
            .cells                   = BATTERY_CELLS_2S,                                   /**< 电池节数: 2S */
            .chg_voltage_mv          = BATTERY_PARAM_2x_NMC_CHG_VOLTAGE_MV,                /**< 总充电终止电压: 8.400V */
            .chg_voltage_per_cell_mv = BATTERY_PARAM_2x_NMC_CHG_VOLTAGE_PER_CELL_MV,       /**< 单节充电终止电压: 4.20V */
            .min_sys_voltage_mv      = BATTERY_PARAM_2x_NMC_MIN_SYS_VOLTAGE_MV,            /**< 最小系统电压: 7.168V */
            .precharge_thresh_mv     = BATTERY_PARAM_2x_NMC_PRECHARGE_THRESH_MV,           /**< 预充电阈值电压: 6.0V */
            .charge_current_ma       = BATTERY_PARAM_2x_NMC_CHARGE_CURRENT_MA,             /**< 典型快充充电电流: 2A */
            .term_current_ma         = BATTERY_PARAM_2x_NMC_TERM_CURRENT_MA,               /**< 充电截止电流: 200mA */
            .sysovp_thresh_mv        = BATTERY_PARAM_2x_NMC_SYSOVP_THRESH_MV               /**< 系统过压保护阈值: 9.4V */
        },
        /* 3S */
        [BATTERY_CELLS_3S] = 
        {
            .chemistry               = BATTERY_CHEM_NMC,                                   /**< 化学类型: 三元锂 */
            .cells                   = BATTERY_CELLS_3S,                                   /**< 电池节数: 3S */
            .chg_voltage_mv          = BATTERY_PARAM_3x_NMC_CHG_VOLTAGE_MV,                /**< 总充电终止电压: 12.600V */
            .chg_voltage_per_cell_mv = BATTERY_PARAM_3x_NMC_CHG_VOLTAGE_PER_CELL_MV,       /**< 单节充电终止电压: 4.20V */
            .min_sys_voltage_mv      = BATTERY_PARAM_3x_NMC_MIN_SYS_VOLTAGE_MV,            /**< 最小系统电压: 10.752V */
            .precharge_thresh_mv     = BATTERY_PARAM_3x_NMC_PRECHARGE_THRESH_MV,           /**< 预充电阈值电压: 9.0V */
            .charge_current_ma       = BATTERY_PARAM_3x_NMC_CHARGE_CURRENT_MA,             /**< 典型快充充电电流: 2A */
            .term_current_ma         = BATTERY_PARAM_3x_NMC_TERM_CURRENT_MA,               /**< 充电截止电流: 200mA */
            .sysovp_thresh_mv        = BATTERY_PARAM_3x_NMC_SYSOVP_THRESH_MV               /**< 系统过压保护阈值: 13.6V */
        },
        /* 4S */
        [BATTERY_CELLS_4S] = 
        {
            .chemistry               = BATTERY_CHEM_NMC,                                   /**< 化学类型: 三元锂 */
            .cells                   = BATTERY_CELLS_4S,                                   /**< 电池节数: 4S */
            .chg_voltage_mv          = BATTERY_PARAM_4x_NMC_CHG_VOLTAGE_MV,                /**< 总充电终止电压: 16.800V */
            .chg_voltage_per_cell_mv = BATTERY_PARAM_4x_NMC_CHG_VOLTAGE_PER_CELL_MV,       /**< 单节充电终止电压: 4.20V */
            .min_sys_voltage_mv      = BATTERY_PARAM_4x_NMC_MIN_SYS_VOLTAGE_MV,            /**< 最小系统电压: 14.336V */
            .precharge_thresh_mv     = BATTERY_PARAM_4x_NMC_PRECHARGE_THRESH_MV,           /**< 预充电阈值电压: 12.0V */
            .charge_current_ma       = BATTERY_PARAM_4x_NMC_CHARGE_CURRENT_MA,             /**< 典型快充充电电流: 2A */
            .term_current_ma         = BATTERY_PARAM_4x_NMC_TERM_CURRENT_MA,               /**< 充电截止电流: 200mA */
            .sysovp_thresh_mv        = BATTERY_PARAM_4x_NMC_SYSOVP_THRESH_MV               /**< 系统过压保护阈值: 17.8V */
        }
    }
};


