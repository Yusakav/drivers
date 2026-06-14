/**
 * @file battery.h
 * @author nyarukov (luckychaoyue1@gmail.com)
 * @brief 电池参数配置查找表
 * @version 0.1
 * @date 2026-06-13
 * 
 * @copyright Copyright (c) 2026
 * 
 */


#ifndef BATTERY_PARAMETER_H
#define BATTERY_PARAMETER_H

#include "stdint.h"

/* ================================================================
 *  电池参数定义表 (Battery Parameter Table)
 *
 *  以分组宏定义形式覆盖 1S~4S 磷酸铁锂 / 三元锂的完整参数集。
 *  命名规则: BATTERY_PARAM_{S}x_{化学类型}_{参数名}
 *
 *  ┌──────────┬──────┬──────────────┬───────────┬──────────┬──────────┬──────────┐
 *  │ 电池类型  │ 节数  │ 充电终止电压   │ 总充电电压  │ 最小系统  │ 预充电阈值 │ 典型充电  │
 *  │          │      │ /cell        │           │ 电压      │          │ 电流     │
 *  ├──────────┼──────┼──────────────┼───────────┼──────────┼──────────┼──────────┤
 *  │ LiFePO4  │ 1S   │ 3.60V        │ 3.600V    │ 3.072V   │ 3.0V     │ 1A       │
 *  │ LiFePO4  │ 2S   │ 3.60V        │ 7.200V    │ 6.144V   │ 6.0V     │ 2A       │
 *  │ LiFePO4  │ 3S   │ 3.60V        │ 10.800V   │ 9.216V   │ 9.0V     │ 2A       │
 *  │ LiFePO4  │ 4S   │ 3.60V        │ 14.400V   │ 12.288V  │ 12.0V    │ 2A       │
 *  │ Li-ion   │ 1S   │ 4.20V        │ 4.200V    │ 3.584V   │ 3.0V     │ 1A       │
 *  │ (NMC)    │ 2S   │ 4.20V        │ 8.400V    │ 7.168V   │ 6.0V     │ 2A       │
 *  │          │ 3S   │ 4.20V        │ 12.600V   │ 10.752V  │ 9.0V     │ 2A       │
 *  │          │ 4S   │ 4.20V        │ 16.800V   │ 14.336V  │ 12.0V    │ 2A       │
 *  └──────────┴──────┴──────────────┴───────────┴──────────┴──────────┴──────────┘
 *
 *  充电终止判断: 当 CV 阶段电流降至典型充电电流的 10% (C/10) 时认为充满。
 *  预充电电流: 硬件自动钳位 (1S≈384mA LDO 模式; 2S~4S 由硬件限制)。
 * ================================================================ */


/** 电池化学类型 */
typedef enum {
    BATTERY_CHEM_UNKNOWN = -1,  /**< 未知化学类型 */
    BATTERY_CHEM_LIFEPO4 = 0,   /**< 磷酸铁锂 (LiFePO4) */
    BATTERY_CHEM_NMC     = 1,   /**< 三元锂 (NMC / Li-ion) */
    BATTERY_CHEM_MAX     = 2,   /**< 最大化学类型 */
} battery_chemistry_t;

/** 电池串联节数 */
typedef enum {
    BATTERY_CELLS_UNKNOWN = -1, /**< 未知节数 */
    BATTERY_CELLS_1S      = 0,  /**< 1S 串联 */
    BATTERY_CELLS_2S      = 1,  /**< 2S 串联 */
    BATTERY_CELLS_3S      = 2,  /**< 3S 串联 */
    BATTERY_CELLS_4S      = 3,  /**< 4S 串联 */
    BATTERY_CELLS_MAX     = 4,  /**< 最大节数 */
} battery_cell_count_t;

/** 通用电池参数配置结构体 */
typedef struct {
    battery_chemistry_t  chemistry;               /**< 化学类型 */
    battery_cell_count_t cells;                   /**< 串联节数 */
    uint16_t             chg_voltage_mv;          /**< 充电终止总电压 (mV) */
    uint16_t             chg_voltage_per_cell_mv; /**< 单节终止电压 (mV) */
    uint16_t             min_sys_voltage_mv;      /**< 最小系统电压 (mV) */
    uint16_t             precharge_thresh_mv;     /**< 预充电阈值 (mV) */
    uint16_t             charge_current_ma;       /**< 典型充电电流 (mA) */
    uint16_t             term_current_ma;         /**< 终止电流 (mA) */
    uint16_t             sysovp_thresh_mv;        /**< 系统过压保护阈值 (mV) */
} battery_params_t;

 /* ========================================================================
 * 1S~4S 磷酸铁锂 (LiFePO4) 通用宏定义
 * ======================================================================== */
// 1S LiFePO4
#define BATTERY_PARAM_1x_LIFEPO4_CHG_VOLTAGE_MV            3600  /**< 总充电终止电压: 3.600V */
#define BATTERY_PARAM_1x_LIFEPO4_CHG_VOLTAGE_PER_CELL_MV   3600  /**< 单节充电终止电压: 3.60V */
#define BATTERY_PARAM_1x_LIFEPO4_MIN_SYS_VOLTAGE_MV        3072  /**< 最小系统电压: 3.072V */
#define BATTERY_PARAM_1x_LIFEPO4_PRECHARGE_THRESH_MV       3000  /**< 预充电阈值电压: 3.0V */
#define BATTERY_PARAM_1x_LIFEPO4_CHARGE_CURRENT_MA         1000  /**< 典型快充充电电流: 1A */
#define BATTERY_PARAM_1x_LIFEPO4_TERM_CURRENT_MA           100   /**< 充电截止电流(10% C/10): 100mA */
#define BATTERY_PARAM_1x_LIFEPO4_SYSOVP_THRESH_MV          4600  /**< 系统过压保护阈值: 4.6V */

// 2S LiFePO4
#define BATTERY_PARAM_2x_LIFEPO4_CHG_VOLTAGE_MV            7200  /**< 总充电终止电压: 7.200V */
#define BATTERY_PARAM_2x_LIFEPO4_CHG_VOLTAGE_PER_CELL_MV   3600  /**< 单节充电终止电压: 3.60V */
#define BATTERY_PARAM_2x_LIFEPO4_MIN_SYS_VOLTAGE_MV        6144  /**< 最小系统电压: 6.144V */
#define BATTERY_PARAM_2x_LIFEPO4_PRECHARGE_THRESH_MV       6000  /**< 预充电阈值电压: 6.0V */
#define BATTERY_PARAM_2x_LIFEPO4_CHARGE_CURRENT_MA         2000  /**< 典型快充充电电流: 2A */
#define BATTERY_PARAM_2x_LIFEPO4_TERM_CURRENT_MA           200   /**< 充电截止电流(10% C/10): 200mA */
#define BATTERY_PARAM_2x_LIFEPO4_SYSOVP_THRESH_MV          8200  /**< 系统过压保护阈值: 8.2V */

// 3S LiFePO4
#define BATTERY_PARAM_3x_LIFEPO4_CHG_VOLTAGE_MV            10800 /**< 总充电终止电压: 10.800V */
#define BATTERY_PARAM_3x_LIFEPO4_CHG_VOLTAGE_PER_CELL_MV   3600  /**< 单节充电终止电压: 3.60V */
#define BATTERY_PARAM_3x_LIFEPO4_MIN_SYS_VOLTAGE_MV        9216  /**< 最小系统电压: 9.216V */
#define BATTERY_PARAM_3x_LIFEPO4_PRECHARGE_THRESH_MV       9000  /**< 预充电阈值电压: 9.0V */
#define BATTERY_PARAM_3x_LIFEPO4_CHARGE_CURRENT_MA         2000  /**< 典型快充充电电流: 2A */
#define BATTERY_PARAM_3x_LIFEPO4_TERM_CURRENT_MA           200   /**< 充电截止电流(10% C/10): 200mA */
#define BATTERY_PARAM_3x_LIFEPO4_SYSOVP_THRESH_MV          12000 /**< 系统过压保护阈值: 12.0V */


// 4S LiFePO4
#define BATTERY_PARAM_4x_LIFEPO4_CHG_VOLTAGE_MV            14400 /**< 总充电终止电压: 14.400V */
#define BATTERY_PARAM_4x_LIFEPO4_CHG_VOLTAGE_PER_CELL_MV   3600  /**< 单节充电终止电压: 3.60V */
#define BATTERY_PARAM_4x_LIFEPO4_MIN_SYS_VOLTAGE_MV        12288 /**< 最小系统电压: 12.288V */
#define BATTERY_PARAM_4x_LIFEPO4_PRECHARGE_THRESH_MV       12000 /**< 预充电阈值电压: 12.0V */
#define BATTERY_PARAM_4x_LIFEPO4_CHARGE_CURRENT_MA         2000  /**< 典型快充充电电流: 2A */
#define BATTERY_PARAM_4x_LIFEPO4_TERM_CURRENT_MA           200   /**< 充电截止电流(10% C/10): 200mA */
#define BATTERY_PARAM_4x_LIFEPO4_SYSOVP_THRESH_MV          12000 /**< 系统过压保护阈值: 12.0V */



/* ========================================================================
 * 1S~4S 三元锂 (NMC / Li-ion) 通用宏定义
 * ======================================================================== */
// 1S NMC
#define BATTERY_PARAM_1x_NMC_CHG_VOLTAGE_MV                4200  /**< 总充电终止电压: 4.200V */
#define BATTERY_PARAM_1x_NMC_CHG_VOLTAGE_PER_CELL_MV       4200  /**< 单节充电终止电压: 4.20V */
#define BATTERY_PARAM_1x_NMC_MIN_SYS_VOLTAGE_MV            3584  /**< 最小系统电压: 3.584V */
#define BATTERY_PARAM_1x_NMC_PRECHARGE_THRESH_MV           3000  /**< 预充电阈值电压: 3.0V */
#define BATTERY_PARAM_1x_NMC_CHARGE_CURRENT_MA             1000  /**< 典型快充充电电流: 1A */
#define BATTERY_PARAM_1x_NMC_TERM_CURRENT_MA               100   /**< 充电截止电流(10% C/10): 100mA */
#define BATTERY_PARAM_1x_NMC_SYSOVP_THRESH_MV              5200  /**< 系统过压保护阈值: 5.2V */

// 2S NMC
#define BATTERY_PARAM_2x_NMC_CHG_VOLTAGE_MV                8400  /**< 总充电终止电压: 8.400V */
#define BATTERY_PARAM_2x_NMC_CHG_VOLTAGE_PER_CELL_MV       4200  /**< 单节充电终止电压: 4.20V */
#define BATTERY_PARAM_2x_NMC_MIN_SYS_VOLTAGE_MV            7168  /**< 最小系统电压: 7.168V */
#define BATTERY_PARAM_2x_NMC_PRECHARGE_THRESH_MV           6000  /**< 预充电阈值电压: 6.0V */
#define BATTERY_PARAM_2x_NMC_CHARGE_CURRENT_MA             2000  /**< 典型快充充电电流: 2A */
#define BATTERY_PARAM_2x_NMC_TERM_CURRENT_MA               200   /**< 充电截止电流(10% C/10): 200mA */
#define BATTERY_PARAM_2x_NMC_SYSOVP_THRESH_MV              9400  /**< 系统过压保护阈值: 9.4V */

// 3S NMC
#define BATTERY_PARAM_3x_NMC_CHG_VOLTAGE_MV                12600 /**< 总充电终止电压: 12.600V */
#define BATTERY_PARAM_3x_NMC_CHG_VOLTAGE_PER_CELL_MV       4200  /**< 单节充电终止电压: 4.20V */
#define BATTERY_PARAM_3x_NMC_MIN_SYS_VOLTAGE_MV            10752 /**< 最小系统电压: 10.752V */
#define BATTERY_PARAM_3x_NMC_PRECHARGE_THRESH_MV           9000  /**< 预充电阈值电压: 9.0V */
#define BATTERY_PARAM_3x_NMC_CHARGE_CURRENT_MA             2000  /**< 典型快充充电电流: 2A */
#define BATTERY_PARAM_3x_NMC_TERM_CURRENT_MA               200   /**< 充电截止电流(10% C/10): 200mA */
#define BATTERY_PARAM_3x_NMC_SYSOVP_THRESH_MV              13600 /**< 系统过压保护阈值: 13.6V */

// 4S NMC
#define BATTERY_PARAM_4x_NMC_CHG_VOLTAGE_MV                16800 /**< 总充电终止电压: 16.800V */
#define BATTERY_PARAM_4x_NMC_CHG_VOLTAGE_PER_CELL_MV       4200  /**< 单节充电终止电压: 4.20V */
#define BATTERY_PARAM_4x_NMC_MIN_SYS_VOLTAGE_MV            14336 /**< 最小系统电压: 14.336V */
#define BATTERY_PARAM_4x_NMC_PRECHARGE_THRESH_MV           12000 /**< 预充电阈值电压: 12.0V */
#define BATTERY_PARAM_4x_NMC_CHARGE_CURRENT_MA             2000  /**< 典型快充充电电流: 2A */
#define BATTERY_PARAM_4x_NMC_TERM_CURRENT_MA               200   /**< 充电截止电流(10% C/10): 200mA */
#define BATTERY_PARAM_4x_NMC_SYSOVP_THRESH_MV              16000 /**< 系统过压保护阈值: 16.0V */

extern const battery_params_t g_battery_params_table[BATTERY_CHEM_MAX][BATTERY_CELLS_MAX];

/** 计算表数大小，方便后续遍历边界控制 */
#define BATTERY_PARAMS_TABLE_SIZE (sizeof(g_battery_params_table) / sizeof(g_battery_params_table[0]))

#endif /* BATTERY_PARAMETER_H */
